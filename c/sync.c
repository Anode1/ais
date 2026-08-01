/* sync.c -- LAN sync transport: seal an index's merge stream under a one-time token,
 * and apply a received sealed stream. See sync.h. The socket layer (a later piece) only
 * moves the sealed blob between two devices; the crypto + merge happen here.
 *
 * POSIX (open_memstream / fmemopen) and the crypto module are required; without either
 * the two functions compile to inert stubs that return -1 (the transport is unavailable,
 * the rest of ais is unaffected). */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "feed.h"
#include "sync.h"

/* Sealed-plaintext protocol version, the very first byte of every unsealed payload.
 * A future format bumps this; a peer that reads a byte it does not recognize fails
 * LOUDLY (-2 from sync_import_sealed) instead of mis-parsing binary as records. */

#if !defined(_WIN32) && defined(__has_include) && __has_include("crypto/monocypher.h")
#  define SYNC_HAVE 1
#  include "crypto/ais_crypto.h"
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>          /* getaddrinfo: join by hostname, not only a dotted quad */
#  include <sys/stat.h>
#  include <sys/time.h>
#  include <dirent.h>
#  include <fcntl.h>
#  include <time.h>
#  include <unistd.h>
#  include <errno.h>
#  include <signal.h>
#  include <poll.h>
#endif

/* Parse a sync URL into HOST and *PORT. Pure string logic (no sockets), so it is always
 * compiled and unit-testable. See sync.h. */
int sync_parse_url(const char *url, char *host, size_t hostsz, int *port)
{
    const char *p, *slash;
    char *colon;
    size_t plen;

    if (url == NULL || host == NULL || hostsz == 0 || port == NULL)
        return -1;
    *port = AIS_SYNC_PORT;
    p = url;
    if (strncmp(p, "http://", 7) == 0)       p += 7;
    else if (strncmp(p, "https://", 8) == 0) p += 8;
    slash = strchr(p, '/');
    plen = slash ? (size_t)(slash - p) : strlen(p);      /* host[:port], drop any path */
    if (plen >= hostsz) plen = hostsz - 1;
    memcpy(host, p, plen);
    host[plen] = '\0';
    colon = strrchr(host, ':');
    if (colon) { *colon = '\0'; *port = atoi(colon + 1); if (*port <= 0 || *port > 65535) *port = AIS_SYNC_PORT; }
    return (host[0] == '\0') ? -1 : 0;
}

#ifdef SYNC_HAVE


/* Assemble the raw (UNSEALED) bundle: version byte + blob sections + merge stream,
 * the shared core both the file bundle (plaintext) and LAN sync (which seals this)
 * use. Allocates *OUT (caller frees). Enforces the same size cap as the wire. */
int sync_export_plain(ais *a, uint8_t **out, size_t *out_len)
{
    char *buf = NULL;
    size_t blen = 0;
    FILE *ms;
    uint8_t ver = AIS_SYNC_PROTO;

    if (!a || !out || !out_len)
        return -1;
    ms = open_memstream(&buf, &blen);          /* capture version + blobs + merge stream */
    if (ms == NULL)
        return -1;
    if (fwrite(&ver, 1, 1, ms) != 1) { fclose(ms); free(buf); return -1; }
    /* feed_export emits the blob sections itself, so calling export_blobs here
     * shipped every document TWICE: it doubled each bundle and halved the usable
     * size cap, which failed a large document outright. */
    feed_export(a, ms);
    if (fclose(ms) != 0) { free(buf); return -1; }
    if (blen > AIS_SYNC_MAX_BLOB) {            /* cap the plain side too (the import side matches) */
        fprintf(stderr, "sync: index too large (%zu bytes > %lu-byte cap)\n",
                blen, (unsigned long)AIS_SYNC_MAX_BLOB);
        free(buf);
        return -1;
    }
    *out = (uint8_t *)buf;
    *out_len = blen;
    return 0;
}

int sync_export_sealed(ais *a, const char *token, uint8_t **out, size_t *out_len)
{
    uint8_t *buf = NULL;
    size_t blen = 0;
    uint8_t kseal[32];
    int rc;

    if (!a || !token || !out || !out_len)
        return -1;
    if (sync_export_plain(a, &buf, &blen) != 0)
        return -1;

    /* seal under a SUBKEY of the token, not the token itself: the auth proof on the wire
     * never yields this key (domain-separated derivation). */
    aisc_subkey((const uint8_t *)token, strlen(token), "ais-sync-seal-v1", NULL, 0, kseal);
    rc = aisc_seal_key(kseal, buf, blen, out, out_len);
    aisc_wipe(kseal, sizeof kseal);
    if (blen)
        aisc_wipe(buf, blen);                  /* the cleartext export held secrets */
    free(buf);
    return (rc == AISC_OK) ? 0 : -1;
}

/* Rename map: incoming "blobs/X" that collided with a different local file was
 * written as "blobs/X-N"; every such (old -> new) is recorded so record values
 * can be repointed. Almost always empty (the fast path skips the whole rewrite). */
struct renmap { char (*old)[AIS_PATH_MAX]; char (*neu)[AIS_PATH_MAX]; int n, cap; };

static int ren_add(struct renmap *m, const char *old, const char *neu)
{
    if (m->n == m->cap) {
        int cap = m->cap ? m->cap * 2 : 8;
        char (*o)[AIS_PATH_MAX] = realloc(m->old, (size_t)cap * AIS_PATH_MAX);
        char (*e)[AIS_PATH_MAX] = realloc(m->neu, (size_t)cap * AIS_PATH_MAX);
        if (o) m->old = o;
        if (e) m->neu = e;
        if (!o || !e) return -1;
        m->cap = cap;
    }
    snprintf(m->old[m->n], AIS_PATH_MAX, "%s", old);
    snprintf(m->neu[m->n], AIS_PATH_MAX, "%s", neu);
    m->n++;
    return 0;
}

static void ren_free(struct renmap *m)
{
    free(m->old); free(m->neu);
    m->old = NULL; m->neu = NULL; m->n = m->cap = 0;
}

/* Whether the file at PATH already holds exactly WANT[0..wlen). */
static int same_content(const char *path, const uint8_t *want, size_t wlen)
{
    FILE *f;
    struct stat st;
    char buf[8192];
    size_t off = 0, n;
    int eq = 1;

    if (stat(path, &st) != 0 || (size_t)st.st_size != wlen)
        return 0;
    f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    while (eq && off < wlen && (n = fread(buf, 1, sizeof buf, f)) > 0) {
        if (off + n > wlen || memcmp(buf, want + off, n) != 0) eq = 0;
        else off += n;
    }
    if (ferror(f) || off != wlen) eq = 0;
    fclose(f);
    return eq;
}

/* Write DATA[0..dlen) to <dir>/RELPATH ("blobs/X") under the keep-both policy:
 * missing -> write; same content -> skip (dedup); different content -> write to
 * the first free "blobs/X-N" (N inserted before any extension) and record a
 * rename in M. Immutable blobs: an existing file is never overwritten. */
static int import_one_blob(const char *dir, const char *relpath,
                           const uint8_t *data, size_t dlen, struct renmap *m)
{
    char blobsdir[AIS_PATH_MAX], target[AIS_PATH_MAX], rel[AIS_PATH_MAX];
    const char *base, *dot;
    FILE *f;
    int seq;

    if (strncmp(relpath, "blobs/", 6) != 0)
        return -1;
    base = relpath + 6;
    if (base[0] == '\0' || strstr(base, "..") != NULL || strchr(base, '/') != NULL)
        return -1;                             /* keep the write inside blobs/ */
    if (snprintf(blobsdir, sizeof blobsdir, "%s/blobs", dir) >= (int)sizeof blobsdir)
        return -1;
    if (mkdir(blobsdir, 0777) != 0 && errno != EEXIST)
        return -1;

    if (snprintf(target, sizeof target, "%s/%s", blobsdir, base) >= (int)sizeof target)
        return -1;
    if (access(target, F_OK) == 0) {
        if (same_content(target, data, dlen))
            return 0;                          /* identical: dedup, nothing to do */
        /* collision: pick blobs/<stem>-<seq><ext>, insert -seq before the ext */
        dot = strrchr(base, '.');
        for (seq = 1; seq < 100000; seq++) {
            int k;
            if (dot != NULL)
                k = snprintf(rel, sizeof rel, "blobs/%.*s-%d%s",
                             (int)(dot - base), base, seq, dot);
            else
                k = snprintf(rel, sizeof rel, "blobs/%s-%d", base, seq);
            if (k >= (int)sizeof rel)
                return -1;
            if (snprintf(target, sizeof target, "%s/%s", dir, rel) >= (int)sizeof target)
                return -1;
            if (access(target, F_OK) != 0)
                break;                         /* a free name */
        }
        if (seq >= 100000)
            return -1;
        if (ren_add(m, relpath, rel) != 0)
            return -1;
    }
    f = fopen(target, "wb");
    if (f == NULL)
        return -1;
    if (dlen > 0 && fwrite(data, 1, dlen, f) != dlen) { fclose(f); return -1; }
    if (fclose(f) != 0)
        return -1;
    return 0;
}

/* Repoint any record value that is exactly OLD or "aisc:@OLD" to NEU, matching at
 * end-of-line: "|OLD\n" -> "|NEU\n" and "|aisc:@OLD\n" -> "|aisc:@NEU\n". Rewrites
 * the NUL-terminated record text TEXT in place into a fresh malloc'd buffer. */
static char *ren_rewrite(char *text, const struct renmap *m)
{
    int i;
    for (i = 0; i < m->n; i++) {
        char pat[AIS_PATH_MAX + 16], apat[AIS_PATH_MAX + 24];
        char rep[AIS_PATH_MAX + 16], arep[AIS_PATH_MAX + 24];
        int f;
        const char *pats[2], *reps[2];

        snprintf(pat, sizeof pat, "|%s\n", m->old[i]);
        snprintf(rep, sizeof rep, "|%s\n", m->neu[i]);
        snprintf(apat, sizeof apat, "|aisc:@%s\n", m->old[i]);
        snprintf(arep, sizeof arep, "|aisc:@%s\n", m->neu[i]);
        pats[0] = pat;  reps[0] = rep;          /* plain document value           */
        pats[1] = apat; reps[1] = arep;         /* encrypted-document value       */

        for (f = 0; f < 2; f++) {
            size_t plen = strlen(pats[f]), rlen = strlen(reps[f]);
            char *hit;
            while ((hit = strstr(text, pats[f])) != NULL) {
                size_t before = (size_t)(hit - text);
                size_t tail = strlen(hit + plen);
                char *nt = malloc(before + rlen + tail + 1);
                if (nt == NULL) return text;
                memcpy(nt, text, before);
                memcpy(nt + before, reps[f], rlen);
                memcpy(nt + before + rlen, hit + plen, tail + 1);
                free(text);
                text = nt;
            }
        }
    }
    return text;
}

/* Parse + merge a raw (UNSEALED) bundle DATA[0..len): version gate, blob-import
 * loop, then feed the record text into the merge. The shared core both the file
 * bundle (plaintext) and LAN sync (which unseals into this) use. DATA is owned by
 * the caller (not freed/wiped here). Returns 0, -1 (bad args / malformed / I/O),
 * or -2 (unrecognized version byte -- a LOUD failure, never a silent mis-parse). */
int sync_import_plain(ais *a, const uint8_t *data, size_t len)
{
    int ret = -1;
    FILE *mf;
    struct renmap map = { NULL, NULL, 0, 0 };
    size_t off;
    char *rectext = NULL;

    if (!a || !data)
        return -1;

    /* Which container is this? The app's bundle is a version byte followed by
     * exactly the bytes `ais --export` writes, so refusing everything without
     * that byte meant the app could not read a single file the CLI produced --
     * on a product whose promise is "plain files, yours, portable". Sniff instead.
     *
     * The framed device bundle is still refused: it has a binary header, so
     * reading it as records would invent them. And the whole 0x00-0x1F range
     * stays reserved for future container versions, so those still fail LOUDLY
     * rather than being taken for text. */
    if (len < 1)
        return -2;
    if (len >= 4 && memcmp(data, "AISB", 4) == 0)
        return -2;                              /* a folder-sync bundle, not an interchange file */
    if (data[0] == AIS_SYNC_PROTO)
        off = 1;                                /* past the version byte */
    else if (data[0] < 0x20)
        return -2;                              /* a container version we do not know */
    else
        off = 0;                                /* a bare merge stream, or a --dump */

    /* Blob section: each "B|relpath|size\n" header is followed by <size> raw
     * bytes. The first line that does not start with "B|" ends the section and
     * begins the record text (which runs to the end of the payload). */
    while (off < len) {
        const uint8_t *line = data + off;
        const uint8_t *nl;
        size_t linelen;
        char relpath[AIS_PATH_MAX];
        long size;
        const char *bar1, *bar2;
        char hdr[AIS_PATH_MAX + 32];

        if (!(len - off >= 2 && line[0] == 'B' && line[1] == '|'))
            break;                              /* not a blob header: records begin here */
        nl = memchr(line, '\n', len - off);
        if (nl == NULL) goto done;              /* truncated header */
        linelen = (size_t)(nl - line);
        if (linelen >= sizeof hdr) goto done;
        memcpy(hdr, line, linelen);
        hdr[linelen] = '\0';
        bar1 = strchr(hdr + 2, '|');            /* hdr is "B|relpath|size" */
        if (bar1 == NULL) goto done;
        bar2 = strchr(bar1 + 1, '|');
        if (bar2 != NULL) goto done;            /* a '|' in relpath: malformed */
        if ((size_t)(bar1 - (hdr + 2)) >= sizeof relpath) goto done;
        memcpy(relpath, hdr + 2, (size_t)(bar1 - (hdr + 2)));
        relpath[bar1 - (hdr + 2)] = '\0';
        size = atol(bar1 + 1);
        if (size < 0) goto done;
        off += linelen + 1;                     /* consume the header + its '\n' */
        if ((size_t)size > len - off) goto done;  /* content runs past the buffer */
        if (import_one_blob(a->dir, relpath, data + off, (size_t)size, &map) != 0)
            goto done;
        off += (size_t)size;                    /* past the raw content */
    }

    /* Record text = the rest of the payload, NUL-terminated so we can rewrite it. */
    rectext = malloc(len - off + 1);
    if (rectext == NULL) goto done;
    memcpy(rectext, data + off, len - off);
    rectext[len - off] = '\0';
    if (map.n > 0)
        rectext = ren_rewrite(rectext, &map);   /* repoint collided blob values */

    mf = fmemopen(rectext, strlen(rectext), "r");
    if (mf == NULL) goto done;
    feed_import_from(a, mf);                     /* record stream -> merge */
    fclose(mf);
    ret = 0;

done:
    ren_free(&map);
    if (rectext) free(rectext);
    return ret;
}

int sync_import_sealed(ais *a, const char *token, const uint8_t *sealed, size_t len)
{
    uint8_t *plain = NULL;
    size_t plen = 0;
    uint8_t kseal[32];
    int rc, ret;

    if (!a || !token || !sealed)
        return -1;
    /* authenticate FIRST: a wrong token or any tampering fails here, before any merge */
    aisc_subkey((const uint8_t *)token, strlen(token), "ais-sync-seal-v1", NULL, 0, kseal);
    rc = aisc_unseal_key(kseal, sealed, len, &plain, &plen);
    aisc_wipe(kseal, sizeof kseal);
    if (rc != AISC_OK)
        return -1;

    ret = sync_import_plain(a, plain, plen);    /* version gate + blob loop + merge */

    aisc_wipe(plain, plen);                      /* the unsealed cleartext held secrets */
    free(plain);
    return ret;
}

/* ----- the socket layer: move the sealed blob between two devices on the LAN ----- */

static int write_all(int fd, const void *buf, size_t n) {
    const char *p = (const char *)buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        if (w == 0) return -1;
        p += w; n -= (size_t)w;
    }
    return 0;
}

static int read_all(int fd, void *buf, size_t n) {
    char *p = (char *)buf;
    while (n > 0) {
        ssize_t r = read(fd, p, n);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) return -1;                  /* EOF before n bytes */
        p += r; n -= (size_t)r;
    }
    return 0;
}

/* Bound how long a stalled peer can hold us, so the transport never hangs. */
static void set_timeout(int fd, int secs) {
    struct timeval tv;
    tv.tv_sec = secs;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

int sync_serve(ais *a, int port, const char *token, int timeout_s, int bidir) {
    int srv, cli = -1, rc = -1, one = 1;
    long survivals0;
    struct sockaddr_in addr;
    size_t tlen = strlen(token);
    uint8_t challenge[24], proof_want[32], proof_got[32];
    uint8_t *blob = NULL;
    size_t blen = 0;
    unsigned char lenbuf[4];

    srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return -1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   /* LAN, not 127.0.0.1 -- a peer must reach it */
    addr.sin_port = htons((unsigned short)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0) { close(srv); return -2; }   /* port busy */
    if (listen(srv, 1) != 0) { close(srv); return -1; }

    /* Portable accept timeout: SO_RCVTIMEO does NOT bound accept() on BSD/macOS,
     * so wait for an incoming connection with poll() before accepting. */
    {
        struct pollfd pfd;
        pfd.fd = srv; pfd.events = POLLIN; pfd.revents = 0;
        if (poll(&pfd, 1, timeout_s > 0 ? timeout_s * 1000 : -1) <= 0) {
            close(srv); return -1;              /* timeout or error: no peer connected */
        }
    }
    cli = accept(srv, NULL, NULL);
    if (cli < 0) { close(srv); return -1; }
    set_timeout(cli, timeout_s);

    /* Challenge-response: prove the peer knows the token WITHOUT it crossing the wire. Send a
     * fresh random challenge; the peer must return the keyed proof of (token, challenge). */
    survivals0 = a->survivals;               /* see AIS_SYNC_AGAIN in sync.h */
    if (aisc_random(challenge, sizeof challenge) != AISC_OK) goto done;
    if (write_all(cli, challenge, sizeof challenge) != 0) goto done;
    if (read_all(cli, proof_got, sizeof proof_got) != 0) goto done;
    aisc_subkey((const uint8_t *)token, tlen, "ais-sync-auth-v1",
                challenge, sizeof challenge, proof_want);
    if (!aisc_verify(proof_got, proof_want))
        goto done;                              /* wrong token: serve nothing */

    if (sync_export_sealed(a, token, &blob, &blen) != 0) goto done;
    lenbuf[0] = (unsigned char)(blen >> 24);
    lenbuf[1] = (unsigned char)(blen >> 16);
    lenbuf[2] = (unsigned char)(blen >> 8);
    lenbuf[3] = (unsigned char)(blen);
    if (write_all(cli, lenbuf, 4) == 0 && write_all(cli, blob, blen) == 0) {
        if (!bidir) {
            rc = 0;
        } else {
            /* symmetric exchange: also receive and merge the peer's sealed stream,
             * which the seal (under the same token) authenticates -- so both sides
             * converge in one connection, with no sender/receiver role. */
            unsigned char rlen[4];
            uint8_t *rblob = NULL;
            size_t rblen = 0;
            /* Our stream is already on the wire and the peer merges it, so from
             * here on the exchange is at least HALF done. Say so: a failure to
             * read their half back is not "nothing happened", and reporting it
             * as one told the user their records had not reached the other
             * device when they had. */
            rc = AIS_SYNC_PARTIAL;
            if (read_all(cli, rlen, 4) == 0) {
                rblen = ((size_t)rlen[0] << 24) | ((size_t)rlen[1] << 16)
                      | ((size_t)rlen[2] << 8) | (size_t)rlen[3];
                if (rblen > 0 && rblen <= AIS_SYNC_MAX_BLOB) {
                    rblob = malloc(rblen);
                    if (rblob && read_all(cli, rblob, rblen) == 0
                        && sync_import_sealed(a, token, rblob, rblen) == 0)
                        rc = 0;
                }
            }
            if (rblob) { aisc_wipe(rblob, rblen); free(rblob); }
        }
    }

done:
    aisc_wipe(challenge, sizeof challenge);
    aisc_wipe(proof_want, sizeof proof_want);
    aisc_wipe(proof_got, sizeof proof_got);
    if (blob) { aisc_wipe(blob, blen); free(blob); }
    if (cli >= 0) close(cli);
    close(srv);
    /* Our stream went out BEFORE we read theirs, so a survival decided while
     * merging it cannot have reached them. They still hold the delete. */
    if (rc == 0 && a->survivals != survivals0)
        rc = AIS_SYNC_AGAIN;
    return rc;
}

int sync_pull(ais *a, const char *host, int port, const char *token, int timeout_s, int bidir) {
    int fd, rc = -1, attempt;
    struct sockaddr_in addr;
    unsigned char lenbuf[4];
    uint8_t challenge[24], proof[32];
    size_t blen;
    uint8_t *blob = NULL;

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        /* Not a dotted quad, so resolve it. Only a literal was accepted before,
         * which meant the names people actually have for their own machines --
         * "mylaptop.local" from mDNS, a router's DHCP name, an /etc/hosts entry
         * -- failed as the generic "could not sync", indistinguishable from a
         * wrong token or the host not running. getaddrinfo covers all three and
         * costs nothing on the literal path, which inet_pton has already taken. */
        struct addrinfo hints, *res = NULL;
        char portstr[16];
        int gai;

        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;               /* the wire is IPv4 (see sync.h) */
        hints.ai_socktype = SOCK_STREAM;
        snprintf(portstr, sizeof portstr, "%d", port);
        gai = getaddrinfo(host, portstr, &hints, &res);
        if (gai != 0 || res == NULL)
            return -1;
        addr.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }

    /* Recreate the socket each attempt: after a failed connect(), BSD/macOS will not
     * let you connect() the same fd again (only Linux retries an unconnected fd cleanly). */
    fd = -1;
    for (attempt = 0; attempt < 50; attempt++) {        /* server may still be binding */
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        set_timeout(fd, timeout_s);
        if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) break;
        close(fd); fd = -1;
        { struct timespec ts = { 0, 100000000L }; nanosleep(&ts, NULL); }   /* 100ms */
    }
    if (fd < 0) return -1;                              /* never connected */

    /* answer the server's challenge with the keyed proof; the token never goes on the wire */
    if (read_all(fd, challenge, sizeof challenge) != 0) goto done;
    aisc_subkey((const uint8_t *)token, strlen(token), "ais-sync-auth-v1",
                challenge, sizeof challenge, proof);
    if (write_all(fd, proof, sizeof proof) != 0) goto done;

    if (read_all(fd, lenbuf, 4) != 0) goto done;
    blen = ((size_t)lenbuf[0] << 24) | ((size_t)lenbuf[1] << 16)
         | ((size_t)lenbuf[2] << 8) | (size_t)lenbuf[3];
    if (blen == 0 || blen > AIS_SYNC_MAX_BLOB) goto done;   /* match the seal-side cap */
    blob = malloc(blen);
    if (!blob) goto done;
    if (read_all(fd, blob, blen) != 0) goto done;

    rc = sync_import_sealed(a, token, blob, blen);

    if (rc == 0 && bidir) {
        /* symmetric exchange: seal and send our own stream back so the peer
         * converges too. Our index now includes what we just merged; the peer
         * re-merges idempotently.
         *
         * The local merge above has ALREADY succeeded, so a failure from here is
         * partial, not total: this device has the peer's records and keeps them.
         * Returning -1 here reported a sync that had half worked as one that had
         * not worked at all -- the wrong half to lie about, since the user is
         * being told whether their data is safe. */
        uint8_t *mine = NULL;
        size_t mlen = 0;
        rc = AIS_SYNC_PARTIAL;
        if (sync_export_sealed(a, token, &mine, &mlen) == 0 && mlen > 0) {
            unsigned char slen[4];
            slen[0] = (unsigned char)(mlen >> 24);
            slen[1] = (unsigned char)(mlen >> 16);
            slen[2] = (unsigned char)(mlen >> 8);
            slen[3] = (unsigned char)(mlen);
            if (write_all(fd, slen, 4) == 0 && write_all(fd, mine, mlen) == 0)
                rc = 0;
        }
        if (mine) { aisc_wipe(mine, mlen); free(mine); }
    }

done:
    aisc_wipe(challenge, sizeof challenge);
    aisc_wipe(proof, sizeof proof);
    if (blob) free(blob);
    close(fd);
    return rc;
}

/* ----- high-level CLI wrappers (token + pairing print + URL parse) ----- */

/* The primary LAN IP, via the connect-a-UDP-socket trick (no packet is sent). */
static int sync_local_ip(char *buf, size_t n) {
    int fd;
    struct sockaddr_in to, me;
    socklen_t ml = sizeof me;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);
    if (connect(fd, (struct sockaddr *)&to, sizeof to) != 0) { close(fd); return -1; }
    if (getsockname(fd, (struct sockaddr *)&me, &ml) != 0) { close(fd); return -1; }
    close(fd);
    return (inet_ntop(AF_INET, &me.sin_addr, buf, n) == NULL) ? -1 : 0;
}

int sync_serve_lan(ais *a, int port, int timeout_s, int bidir) {
    char token[33], ip[64];

    signal(SIGPIPE, SIG_IGN);                  /* a peer that vanishes mid-write must not kill us */
    if (aisc_token(token, sizeof token) != AISC_OK) {
        fprintf(stderr, "sync: cannot generate a token\n");
        return -1;
    }
    if (sync_local_ip(ip, sizeof ip) != 0)
        snprintf(ip, sizeof ip, "<this-device-ip>");

    printf("AIS LAN sync: serving one peer for up to %ds. On the other device run:\n\n", timeout_s);
    printf("    ais %s http://%s:%d --token %s\n\n",
           bidir ? "--sync" : "--import", ip, port, token);
    fflush(stdout);

    {
        int rc = sync_serve(a, port, token, timeout_s, bidir);
        if (rc == AIS_SYNC_PARTIAL) {
            /* The peer HAS our records -- our stream went out and it merged --
             * we just never got its half back. Reported as a failure, that read
             * as "nothing was copied", which is the opposite of what happened. */
            fprintf(stderr, "sync: the other device got your records, but its own did not come back.\n"
                            "      Nothing was lost. Run the same command again to finish.\n");
            aisc_wipe(token, sizeof token);
            return 1;
        }
        if (rc == AIS_SYNC_AGAIN) {
            printf("sync: exchanged with a peer. A record here outlived a delete that\n"
                   "      arrived in this round, and that news could not go out until the\n"
                   "      next one -- run the same command once more so both devices match.\n");
            aisc_wipe(token, sizeof token);
            return 2;
        }
        if (rc != 0) {
            fprintf(stderr, "sync: no peer completed (timeout, wrong token, or error)\n");
            aisc_wipe(token, sizeof token);
            return -1;
        }
    }
    printf("sync: a peer pulled and merged successfully.\n");
    aisc_wipe(token, sizeof token);
    return 0;
}

int sync_pull_url(ais *a, const char *url, const char *token, int timeout_s, int bidir) {
    char host[128];
    int port;

    signal(SIGPIPE, SIG_IGN);
    if (!url || !token) {
        fprintf(stderr, "sync: needs <url> and --token TOKEN\n");
        return -1;
    }
    if (sync_parse_url(url, host, sizeof host, &port) != 0) {
        fprintf(stderr, "sync: bad url '%s'\n", url);
        return -1;
    }
    {
        int rc = sync_pull(a, host, port, token, timeout_s, bidir);
        if (rc == -2) {                        /* version byte mismatch: loud, actionable */
            fprintf(stderr, "sync: the other device runs a different AIS version -- update both.\n");
            return -1;
        }
        if (rc == AIS_SYNC_PARTIAL) {
            /* Half done, and the half that worked is the half that matters here:
             * this device HAS the peer's records. Do not call that a failure --
             * but do not call it "converged" either, which is what sent a user
             * away believing both devices matched when only one had changed. */
            fprintf(stderr, "sync: got %s:%d's records, but could not send yours back.\n"
                            "      Nothing was lost. Run the same command again to finish.\n",
                    host, port);
            return 1;
        }
        if (rc != 0) {
            fprintf(stderr, "sync: exchange with %s:%d failed (wrong token, timeout, or no server there)\n",
                    host, port);
            return -1;
        }
    }
    printf("sync: %s %s:%d\n", bidir ? "converged with" : "merged from", host, port);
    return 0;
}

/* ===== folder auto-sync: a framed plaintext bundle per device in a shared folder,
 * moved by an external tool (Syncthing / a cloud folder). The merge is already
 * conflict-free; this adds the FILE-LEVEL protocol the design review found missing:
 * a length+checksum frame so a torn/partial read is REJECTED (never merged), and a
 * per-writer nonce so a cloned device-id is detected and healed. See spec v1. ===== */

/* File frame (little-endian ints): "AISB" | ver(1) | nonce(16) | seq(8) |
 * payload_len(8) | crc32(4) | payload. Payload = sync_export_plain's bytes.
 * seq is a per-writer monotonic counter: a device that finds its own file
 * advanced past its last-written seq knows an identical clone shares its id. */
#define AIS_FRAME_HDR   41
#define AIS_FRAME_VER   1

static uint32_t crc32_of(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    size_t i; int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
    }
    return c ^ 0xFFFFFFFFu;
}
static void put_u64le(uint8_t *p, uint64_t v) { int i; for (i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint64_t get_u64le(const uint8_t *p) { uint64_t v = 0; int i; for (i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }
static void put_u32le(uint8_t *p, uint32_t v) { int i; for (i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get_u32le(const uint8_t *p) { uint32_t v = 0; int i; for (i = 0; i < 4; i++) v |= (uint32_t)p[i] << (8 * i); return v; }

static int rand_bytes(uint8_t *p, size_t n) {
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got;
    if (f == NULL) return -1;
    got = fread(p, 1, n, f);
    fclose(f);
    return got == n ? 0 : -1;
}
static void hexof(const uint8_t *p, size_t n, char *out) {
    static const char h[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) { out[2 * i] = h[p[i] >> 4]; out[2 * i + 1] = h[p[i] & 15]; }
    out[2 * n] = '\0';
}
static int unhex(const char *s, uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i < 2 * n; i++) {
        char c = s[i]; int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else return -1;
        if (i & 1) p[i / 2] |= (uint8_t)v; else p[i / 2] = (uint8_t)(v << 4);
    }
    return 0;
}

/* A device-id filename is exactly 16 lowercase-hex chars + ".aisb" (21 chars). */
static int is_bundle_name(const char *name) {
    size_t i;
    if (strlen(name) != 21) return 0;
    for (i = 0; i < 16; i++)
        if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f')))
            return 0;
    return strcmp(name + 16, ".aisb") == 0;
}

/* This device's sync identity, persisted in <index>/syncid as "id nonce seq". */
struct sync_ident { char id[17]; uint8_t nonce[16]; uint64_t seq; };

static int ident_save(ais *a, const struct sync_ident *s) {
    char path[AIS_PATH_MAX], tmp[AIS_PATH_MAX], noncehex[33];
    FILE *f;
    hexof(s->nonce, 16, noncehex);
    if (snprintf(path, sizeof path, "%s/syncid", a->dir) >= (int)sizeof path) return -1;
    if (snprintf(tmp, sizeof tmp, "%s/syncid.tmp", a->dir) >= (int)sizeof tmp) return -1;
    f = fopen(tmp, "w");
    if (f == NULL) return -1;
    fprintf(f, "%s %s %llu\n", s->id, noncehex, (unsigned long long)s->seq);
    if (fclose(f) != 0) { remove(tmp); return -1; }
    if (rename(tmp, path) != 0) { remove(tmp); return -1; }
    return 0;
}
static int ident_new(ais *a, struct sync_ident *s) {
    uint8_t idb[8];
    if (rand_bytes(idb, 8) != 0 || rand_bytes(s->nonce, 16) != 0) return -1;
    hexof(idb, 8, s->id);
    s->seq = 0;
    return ident_save(a, s);
}
static int ident_load(ais *a, struct sync_ident *s) {
    char path[AIS_PATH_MAX], idbuf[32], noncehex[64];
    unsigned long long seq = 0;
    FILE *f;
    if (snprintf(path, sizeof path, "%s/syncid", a->dir) >= (int)sizeof path) return -1;
    f = fopen(path, "r");
    if (f != NULL) {
        int ok = (fscanf(f, "%31s %63s %llu", idbuf, noncehex, &seq) >= 2)
                 && strlen(idbuf) == 16 && strlen(noncehex) == 32
                 && unhex(noncehex, s->nonce, 16) == 0;
        fclose(f);
        if (ok) { memcpy(s->id, idbuf, 17); s->seq = (uint64_t)seq; return 0; }
        /* malformed: regenerate below */
    }
    return ident_new(a, s);
}

/* Public thin wrappers (mainly for tests to inspect/force identity). */
int sync_device_id(ais *a, char *id_hex, size_t idsz, uint8_t nonce[16]) {
    struct sync_ident s;
    if (!a || !id_hex || idsz < 17 || !nonce) return -1;
    if (ident_load(a, &s) != 0) return -1;
    memcpy(id_hex, s.id, 17);
    memcpy(nonce, s.nonce, 16);
    return 0;
}
int sync_device_new(ais *a, char *id_hex, size_t idsz, uint8_t nonce[16]) {
    struct sync_ident s;
    if (!a || !id_hex || idsz < 17 || !nonce) return -1;
    if (ident_new(a, &s) != 0) return -1;
    memcpy(id_hex, s.id, 17);
    memcpy(nonce, s.nonce, 16);
    return 0;
}

int sync_export_framed(ais *a, const uint8_t nonce[16], uint64_t seq,
                       uint8_t **out, size_t *out_len) {
    uint8_t *payload = NULL, *buf;
    size_t plen = 0, total;
    if (!a || !nonce || !out || !out_len) return -1;
    if (sync_export_plain(a, &payload, &plen) != 0) return -1;
    total = AIS_FRAME_HDR + plen;
    buf = malloc(total);
    if (buf == NULL) { free(payload); return -1; }
    memcpy(buf, "AISB", 4);
    buf[4] = AIS_FRAME_VER;
    memcpy(buf + 5, nonce, 16);
    put_u64le(buf + 21, seq);
    put_u64le(buf + 29, (uint64_t)plen);
    put_u32le(buf + 37, crc32_of(payload, plen));
    memcpy(buf + AIS_FRAME_HDR, payload, plen);
    free(payload);
    *out = buf;
    *out_len = total;
    return 0;
}

/* Verify the frame and merge the payload. REJECTS (-1) a short/truncated/corrupt
 * file wholesale before a byte reaches the merge (the core B2 guarantee). -2 on an
 * unknown frame version. On success NONCE_OUT / SEQ_OUT (if given) get the writer's. */
int sync_import_framed(ais *a, const uint8_t *data, size_t len,
                       uint8_t nonce_out[16], uint64_t *seq_out) {
    uint64_t plen;
    if (!a || !data) return -1;
    if (len < AIS_FRAME_HDR) return -1;                 /* too short to be a frame */
    if (memcmp(data, "AISB", 4) != 0) return -1;        /* not our magic */
    if (data[4] != AIS_FRAME_VER) return -2;            /* unknown frame version */
    plen = get_u64le(data + 29);
    if (plen != (uint64_t)(len - AIS_FRAME_HDR)) return -1;   /* truncated/partial */
    if (crc32_of(data + AIS_FRAME_HDR, (size_t)plen) != get_u32le(data + 37))
        return -1;                                       /* corrupt payload */
    if (nonce_out) memcpy(nonce_out, data + 5, 16);
    if (seq_out) *seq_out = get_u64le(data + 21);
    return sync_import_plain(a, data + AIS_FRAME_HDR, (size_t)plen);
}

/* Read a whole file into a malloc'd buffer (caller frees). 0 on success. */
static int read_file(const char *path, uint8_t **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    long sz;
    uint8_t *buf;
    if (f == NULL) return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    if ((size_t)sz > AIS_SYNC_MAX_BLOB + AIS_FRAME_HDR + 4096) { fclose(f); return -1; }
    buf = malloc((size_t)sz ? (size_t)sz : 1);
    if (buf == NULL) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(buf); return -1; }
    fclose(f);
    *out = buf;
    *out_len = (size_t)sz;
    return 0;
}

/* Read a bundle file's writer-nonce + seq from its header (for clone detection). 0 if
 * a valid frame header was read, -1 otherwise (missing / not a frame). */
static int read_frame_meta(const char *path, uint8_t nonce[16], uint64_t *seq) {
    FILE *f = fopen(path, "rb");
    uint8_t hdr[AIS_FRAME_HDR];
    if (f == NULL) return -1;
    if (fread(hdr, 1, AIS_FRAME_HDR, f) != AIS_FRAME_HDR) { fclose(f); return -1; }
    fclose(f);
    if (memcmp(hdr, "AISB", 4) != 0 || hdr[4] != AIS_FRAME_VER) return -1;
    memcpy(nonce, hdr + 5, 16);
    if (seq) *seq = get_u64le(hdr + 21);
    return 0;
}

/* Atomically write DATA to PATH: tmp -> fsync -> rename -> fsync(dir). */
static int atomic_write(const char *dir, const char *path, const uint8_t *data, size_t len) {
    char tmp[AIS_PATH_MAX];
    FILE *f;
    int dfd;
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
    f = fopen(tmp, "wb");
    if (f == NULL) return -1;
    if (fwrite(data, 1, len, f) != len) { fclose(f); remove(tmp); return -1; }
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) { fclose(f); remove(tmp); return -1; }
    if (fclose(f) != 0) { remove(tmp); return -1; }
    if (rename(tmp, path) != 0) { remove(tmp); return -1; }
    dfd = open(dir, O_RDONLY);
    if (dfd >= 0) { fsync(dfd); close(dfd); }
    return 0;
}

/* Folders this device has already synced with, one absolute path per line in
 * INDEX/foldsync. The point is to tell "a folder I have never used" (fine, first
 * pass) apart from "the folder I have been backing up to, which no longer holds
 * my bundle" (an unmounted drive, a wiped share, a replaced USB stick). Existence
 * cannot make that distinction: an unmounted drive's mount point is an ordinary
 * empty directory, which is exactly what a working folder looks like on day one. */
/* The canonical path, or "" when it cannot be trusted as a key. A path that does
 * not resolve, or that contains the newline this file uses as its separator, is
 * simply not remembered: a memory that can match the WRONG folder is worse than
 * no memory at all. (realpath's contract is a PATH_MAX buffer; AIS_PATH_MAX is
 * PATH_MAX here, and the assert keeps that from drifting.) */
static void fold_canon(const char *folder, char *out, size_t outsz)
{
    char buf[AIS_PATH_MAX];

    out[0] = '\0';
    if (AIS_PATH_MAX < 4096)
        return;
    if (realpath(folder, buf) == NULL)
        return;
    if (strchr(buf, '\n') != NULL)
        return;
    snprintf(out, outsz, "%s", buf);
}

static int fold_known(const ais *a, const char *canon)
{
    char path[AIS_PATH_MAX], line[AIS_PATH_MAX];
    FILE *f;
    int found = 0;

    if (canon[0] == '\0')
        return 0;
    if (snprintf(path, sizeof path, "%s/foldsync", a->dir) >= (int)sizeof path)
        return 0;
    f = fopen(path, "r");
    if (f == NULL)
        return 0;
    while (!found && fgets(line, sizeof line, f) != NULL) {
        char *nl = strchr(line, '\n');
        if (nl != NULL) *nl = '\0';
        if (strcmp(line, canon) == 0) found = 1;
    }
    fclose(f);
    return found;
}

static void fold_remember(const ais *a, const char *canon)
{
    char path[AIS_PATH_MAX];
    FILE *f;

    if (canon[0] == '\0' || fold_known(a, canon))
        return;
    if (snprintf(path, sizeof path, "%s/foldsync", a->dir) >= (int)sizeof path)
        return;
    f = fopen(path, "a");
    if (f == NULL)
        return;                                /* best effort: this is a warning aid */
    fprintf(f, "%s\n", canon);
    fclose(f);
}

/* Does FOLDER hold any device bundle at all? */
static int folder_has_bundle(const char *folder)
{
    DIR *d = opendir(folder);
    struct dirent *de;
    int found = 0;

    if (d == NULL)
        return -1;
    while (!found && (de = readdir(d)) != NULL)
        if (is_bundle_name(de->d_name))
            found = 1;
    closedir(d);
    return found;
}

/* One folder-sync pass. Import every well-formed peer bundle, then (re)write our own
 * <id>.aisb atomically. Heals a device-id clone: if our own file carries a nonce that
 * isn't ours, a sibling device shares our id, so regenerate ours.
 * Returns 0 or one of the AIS_FOLDER_* codes in sync.h. */
int sync_folder_once(ais *a, const char *folder)
{
    return sync_folder_once_force(a, folder, 0);
}

int sync_folder_once_force(ais *a, const char *folder, int force) {
    struct sync_ident s;
    char own_name[32], own_path[AIS_PATH_MAX], canon[AIS_PATH_MAX];
    uint8_t fnonce[16];
    uint64_t fseq = 0;
    uint8_t *bundle = NULL;
    size_t blen = 0;
    DIR *d;
    struct dirent *de;
    int rc = -1;

    if (!a || !folder) return -1;
    {
        /* The folder must ALREADY exist. Creating it turned a typo, or an SD card
         * that was not mounted, into a brand-new empty directory that this device
         * happily "synced" with forever -- reporting success while nothing ever
         * arrived from the other side. Sync is the backup here, so a silent
         * no-op is the worst outcome available. Refuse instead; a first-time
         * user creating the folder themselves is a far cheaper cost. */
        struct stat st;
        if (stat(folder, &st) != 0)
            /* Only ENOENT means "not there". EACCES, ELOOP, EIO and -- the one that
             * matters here -- ENOTCONN from a stale FUSE mount are real conditions
             * with real remedies, and telling that user to create a folder that
             * already exists sends them the wrong way. */
            return (errno == ENOENT) ? AIS_FOLDER_MISSING : AIS_FOLDER_STAT;
        if (!S_ISDIR(st.st_mode))
            return AIS_FOLDER_NOTDIR;
    }
    if (ident_load(a, &s) != 0) return -1;
    snprintf(own_name, sizeof own_name, "%s.aisb", s.id);
    snprintf(own_path, sizeof own_path, "%s/%s", folder, own_name);

    /* The folder exists and is a directory -- which is also true of an unmounted
     * drive's mount point and of a share somebody emptied. If we have synced here
     * before, it must still hold at least one device bundle.
     *
     * ANY bundle, deliberately, not this device's own. Our own file's name follows
     * our device id, and the clone-heal below changes that id whenever a copied
     * index turns up -- which the recommended whole-directory Syncthing setup makes
     * an ordinary event. Keying on our own name meant that one heal made every
     * other remembered folder look wiped, and told the user their drive was
     * unplugged when it was sitting there intact. An empty folder is the honest
     * signal: it is what a wipe, an unmounted drive, and a mount point that has had
     * a filesystem mounted over it all look like. */
    fold_canon(folder, canon, sizeof canon);
    if (!force && fold_known(a, canon)) {
        int has = folder_has_bundle(folder);
        if (has < 0)
            return AIS_FOLDER_STAT;      /* unreadable: errno says why, not "failed" */
        if (has == 0)
            return AIS_FOLDER_STRANGER;
    }

    /* Clone heal: our own file was written either by a DIFFERENT nonce (another device
     * cloned our id after we diverged) or by our SAME nonce but advanced PAST our last
     * write (a bit-identical clone that copied our whole identity). Either way a sibling
     * shares our id and would clobber us: take a fresh identity. */
    if (read_frame_meta(own_path, fnonce, &fseq) == 0) {
        int diff_nonce = memcmp(fnonce, s.nonce, 16) != 0;
        if (diff_nonce || fseq > s.seq) {
            if (ident_new(a, &s) != 0) return -1;
            snprintf(own_name, sizeof own_name, "%s.aisb", s.id);
            snprintf(own_path, sizeof own_path, "%s/%s", folder, own_name);
        }
    }

    /* Import pass: every well-formed peer bundle (skip our own; skip corrupt/partial). */
    d = opendir(folder);
    if (d == NULL) return -1;
    while ((de = readdir(d)) != NULL) {
        char path[AIS_PATH_MAX];
        uint8_t *buf = NULL;
        size_t n = 0;
        struct stat st;
        if (!is_bundle_name(de->d_name) || strcmp(de->d_name, own_name) == 0)
            continue;
        if (snprintf(path, sizeof path, "%s/%s", folder, de->d_name) >= (int)sizeof path)
            continue;
        if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;                              /* skip symlinks / non-regular */
        if (read_file(path, &buf, &n) == 0) {
            sync_import_framed(a, buf, n, NULL, NULL);  /* rc<0 (partial/corrupt): skip, retry */
            free(buf);
        }
    }
    closedir(d);

    /* Export pass: bump our seq, write atomically, persist the advanced seq. */
    s.seq += 1;
    if (sync_export_framed(a, s.nonce, s.seq, &bundle, &blen) != 0) return -1;
    rc = atomic_write(folder, own_path, bundle, blen);
    free(bundle);
    if (rc != 0)
        return AIS_FOLDER_NOWRITE;   /* read-only remount, full disk: imports already applied */
    ident_save(a, &s);
    fold_remember(a, canon);
    return 0;
}

#else  /* no POSIX buffer streams or no crypto: transport unavailable */

int sync_export_plain(ais *a, uint8_t **out, size_t *out_len)
{
    (void)a; (void)out; (void)out_len;
    return -1;
}
int sync_import_plain(ais *a, const uint8_t *data, size_t len)
{
    (void)a; (void)data; (void)len;
    return -1;
}
int sync_export_sealed(ais *a, const char *token, uint8_t **out, size_t *out_len)
{
    (void)a; (void)token; (void)out; (void)out_len;
    return -1;
}
int sync_import_sealed(ais *a, const char *token, const uint8_t *sealed, size_t len)
{
    (void)a; (void)token; (void)sealed; (void)len;
    return -1;
}
int sync_serve(ais *a, int port, const char *token, int timeout_s, int bidir)
{
    (void)a; (void)port; (void)token; (void)timeout_s; (void)bidir;
    return -1;
}
int sync_pull(ais *a, const char *host, int port, const char *token, int timeout_s, int bidir)
{
    (void)a; (void)host; (void)port; (void)token; (void)timeout_s; (void)bidir;
    return -1;
}
int sync_serve_lan(ais *a, int port, int timeout_s, int bidir)
{
    (void)a; (void)port; (void)timeout_s; (void)bidir;
    fprintf(stderr, "sync: this build lacks LAN sync support (needs POSIX + the crypto module)\n");
    return -1;
}
int sync_pull_url(ais *a, const char *url, const char *token, int timeout_s, int bidir)
{
    (void)a; (void)url; (void)token; (void)timeout_s; (void)bidir;
    fprintf(stderr, "sync: this build lacks LAN sync support (needs POSIX + the crypto module)\n");
    return -1;
}
int sync_export_framed(ais *a, const uint8_t nonce[16], uint64_t seq, uint8_t **out, size_t *out_len)
{ (void)a; (void)nonce; (void)seq; (void)out; (void)out_len; return -1; }
int sync_import_framed(ais *a, const uint8_t *data, size_t len, uint8_t nonce_out[16], uint64_t *seq_out)
{ (void)a; (void)data; (void)len; (void)nonce_out; (void)seq_out; return -1; }
int sync_device_id(ais *a, char *id_hex, size_t idsz, uint8_t nonce[16])
{ (void)a; (void)id_hex; (void)idsz; (void)nonce; return -1; }
int sync_device_new(ais *a, char *id_hex, size_t idsz, uint8_t nonce[16])
{ (void)a; (void)id_hex; (void)idsz; (void)nonce; return -1; }
int sync_folder_once(ais *a, const char *folder)
{ (void)a; (void)folder; fprintf(stderr, "sync: this build lacks folder sync (needs POSIX + crypto)\n"); return -1; }
int sync_folder_once_force(ais *a, const char *folder, int force)
{ (void)force; return sync_folder_once(a, folder); }

#endif
