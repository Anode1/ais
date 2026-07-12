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
#define AIS_SYNC_PROTO 1

#if !defined(_WIN32) && defined(__has_include) && __has_include("crypto/monocypher.h")
#  define SYNC_HAVE 1
#  include "crypto/ais_crypto.h"
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <sys/stat.h>
#  include <sys/time.h>
#  include <dirent.h>
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

/* Append one blob's "B|blobs/<name>|<size>\n" header plus its raw bytes to MS.
 * Returns 0, or -1 on an I/O / path / cap error (running total kept in *TOTAL). */
static int export_one_blob(FILE *ms, const char *dir, const char *name, size_t *total)
{
    char path[AIS_PATH_MAX];
    FILE *bf;
    long sz;
    char buf[8192];
    size_t n;

    if (snprintf(path, sizeof path, "%s/blobs/%s", dir, name) >= (int)sizeof path)
        return -1;
    bf = fopen(path, "rb");
    if (bf == NULL)
        return -1;
    if (fseek(bf, 0, SEEK_END) != 0 || (sz = ftell(bf)) < 0 || fseek(bf, 0, SEEK_SET) != 0) {
        fclose(bf);
        return -1;
    }
    /* account header + content against the shared cap before writing anything */
    *total += (size_t)snprintf(buf, sizeof buf, "B|blobs/%s|%ld\n", name, sz) + (size_t)sz;
    if (*total > AIS_SYNC_MAX_BLOB) { fclose(bf); return -1; }
    if (fprintf(ms, "B|blobs/%s|%ld\n", name, sz) < 0) { fclose(bf); return -1; }
    while ((n = fread(buf, 1, sizeof buf, bf)) > 0)
        if (fwrite(buf, 1, n, ms) != n) { fclose(bf); return -1; }
    if (ferror(bf)) { fclose(bf); return -1; }
    fclose(bf);
    return 0;
}

/* Walk <dir>/blobs/, emitting each regular file as a "B|" section into MS.
 * A missing blobs/ dir is fine (no section). Returns 0, or -1 on error/overflow. */
static int export_blobs(FILE *ms, const char *dir, size_t *total)
{
    char blobsdir[AIS_PATH_MAX];
    DIR *d;
    struct dirent *de;
    int rc = 0;

    if (snprintf(blobsdir, sizeof blobsdir, "%s/blobs", dir) >= (int)sizeof blobsdir)
        return -1;
    d = opendir(blobsdir);
    if (d == NULL)
        return 0;                              /* no blobs/ -> no blob section */
    while (rc == 0 && (de = readdir(d)) != NULL) {
        char path[AIS_PATH_MAX];
        struct stat st;
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (snprintf(path, sizeof path, "%s/%s", blobsdir, de->d_name) >= (int)sizeof path)
            { rc = -1; break; }
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;                          /* skip non-regular entries */
        rc = export_one_blob(ms, dir, de->d_name, total);
    }
    closedir(d);
    return rc;
}

/* Assemble the raw (UNSEALED) bundle: version byte + blob sections + merge stream,
 * the shared core both the file bundle (plaintext) and LAN sync (which seals this)
 * use. Allocates *OUT (caller frees). Enforces the same size cap as the wire. */
int sync_export_plain(ais *a, uint8_t **out, size_t *out_len)
{
    char *buf = NULL;
    size_t blen = 0, total = 1;                /* the version byte counts toward the cap */
    FILE *ms;
    uint8_t ver = AIS_SYNC_PROTO;

    if (!a || !out || !out_len)
        return -1;
    ms = open_memstream(&buf, &blen);          /* capture version + blobs + merge stream */
    if (ms == NULL)
        return -1;
    if (fwrite(&ver, 1, 1, ms) != 1) { fclose(ms); free(buf); return -1; }
    if (export_blobs(ms, a->dir, &total) != 0) {
        fprintf(stderr, "sync: index too large (blobs exceed %lu-byte cap)\n",
                (unsigned long)AIS_SYNC_MAX_BLOB);
        fclose(ms);
        free(buf);
        return -1;
    }
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

    /* version gate: a byte we do not recognize is a LOUD failure (-2), never a
     * silent mis-parse of binary as records. */
    if (len < 1 || data[0] != AIS_SYNC_PROTO)
        return -2;
    off = 1;                                    /* past the version byte */

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
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) return -1;

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
         * re-merges idempotently. */
        uint8_t *mine = NULL;
        size_t mlen = 0;
        rc = -1;
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

    if (sync_serve(a, port, token, timeout_s, bidir) != 0) {
        fprintf(stderr, "sync: no peer completed (timeout, wrong token, or error)\n");
        aisc_wipe(token, sizeof token);
        return -1;
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
        if (rc != 0) {
            fprintf(stderr, "sync: exchange with %s:%d failed (wrong token, timeout, or no server there)\n",
                    host, port);
            return -1;
        }
    }
    printf("sync: %s %s:%d\n", bidir ? "converged with" : "merged from", host, port);
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

#endif
