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

#if !defined(_WIN32) && defined(__has_include) && __has_include("crypto/monocypher.h")
#  define SYNC_HAVE 1
#  include "crypto/ais_crypto.h"
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <sys/time.h>
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

int sync_export_sealed(ais *a, const char *token, uint8_t **out, size_t *out_len)
{
    char *buf = NULL;
    size_t blen = 0;
    FILE *ms;
    uint8_t kseal[32];
    int rc;

    if (!a || !token || !out || !out_len)
        return -1;
    ms = open_memstream(&buf, &blen);          /* capture the merge stream to memory */
    if (ms == NULL)
        return -1;
    feed_export(a, ms);
    if (fclose(ms) != 0) { free(buf); return -1; }
    if (blen > AIS_SYNC_MAX_BLOB) {            /* cap the seal side too (the pull side matches) */
        fprintf(stderr, "sync: index too large to seal (%zu bytes > %lu-byte cap)\n",
                blen, (unsigned long)AIS_SYNC_MAX_BLOB);
        if (blen) aisc_wipe(buf, blen);
        free(buf);
        return -1;
    }

    /* seal under a SUBKEY of the token, not the token itself: the auth proof on the wire
     * never yields this key (domain-separated derivation). */
    aisc_subkey((const uint8_t *)token, strlen(token), "ais-sync-seal-v1", NULL, 0, kseal);
    rc = aisc_seal_key(kseal, (const uint8_t *)buf, blen, out, out_len);
    aisc_wipe(kseal, sizeof kseal);
    if (blen)
        aisc_wipe(buf, blen);                  /* the cleartext export held secrets */
    free(buf);
    return (rc == AISC_OK) ? 0 : -1;
}

int sync_import_sealed(ais *a, const char *token, const uint8_t *sealed, size_t len)
{
    uint8_t *plain = NULL;
    size_t plen = 0;
    uint8_t kseal[32];
    int rc;
    FILE *mf;

    if (!a || !token || !sealed)
        return -1;
    /* authenticate FIRST: a wrong token or any tampering fails here, before any merge */
    aisc_subkey((const uint8_t *)token, strlen(token), "ais-sync-seal-v1", NULL, 0, kseal);
    rc = aisc_unseal_key(kseal, sealed, len, &plain, &plen);
    aisc_wipe(kseal, sizeof kseal);
    if (rc != AISC_OK)
        return -1;

    mf = fmemopen(plain, plen, "r");
    if (mf == NULL) { aisc_wipe(plain, plen); free(plain); return -1; }
    feed_import_from(a, mf);                    /* unsealed stream -> merge */
    fclose(mf);
    aisc_wipe(plain, plen);
    free(plain);
    return 0;
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
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0) { close(srv); return -1; }
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

int sync_serve_lan(ais *a, int port, int timeout_s) {
    char token[33], ip[64];

    signal(SIGPIPE, SIG_IGN);                  /* a peer that vanishes mid-write must not kill us */
    if (aisc_token(token, sizeof token) != AISC_OK) {
        fprintf(stderr, "sync: cannot generate a token\n");
        return -1;
    }
    if (sync_local_ip(ip, sizeof ip) != 0)
        snprintf(ip, sizeof ip, "<this-device-ip>");

    printf("AIS LAN sync: serving one peer for up to %ds. On the other device run:\n\n", timeout_s);
    printf("    ais --import http://%s:%d --token %s\n\n", ip, port, token);
    fflush(stdout);

    if (sync_serve(a, port, token, timeout_s, 0) != 0) {
        fprintf(stderr, "sync: no peer completed (timeout, wrong token, or error)\n");
        aisc_wipe(token, sizeof token);
        return -1;
    }
    printf("sync: a peer pulled and merged successfully.\n");
    aisc_wipe(token, sizeof token);
    return 0;
}

int sync_pull_url(ais *a, const char *url, const char *token, int timeout_s) {
    char host[128];
    int port;

    signal(SIGPIPE, SIG_IGN);
    if (!url || !token) {
        fprintf(stderr, "sync: --import <url> needs --token TOKEN\n");
        return -1;
    }
    if (sync_parse_url(url, host, sizeof host, &port) != 0) {
        fprintf(stderr, "sync: bad url '%s'\n", url);
        return -1;
    }
    if (sync_pull(a, host, port, token, timeout_s, 0) != 0) {
        fprintf(stderr, "sync: pull from %s:%d failed (wrong token, timeout, or no server there)\n",
                host, port);
        return -1;
    }
    printf("sync: merged from %s:%d\n", host, port);
    return 0;
}

#else  /* no POSIX buffer streams or no crypto: transport unavailable */

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
int sync_serve_lan(ais *a, int port, int timeout_s)
{
    (void)a; (void)port; (void)timeout_s;
    fprintf(stderr, "sync: this build lacks LAN sync support (needs POSIX + the crypto module)\n");
    return -1;
}
int sync_pull_url(ais *a, const char *url, const char *token, int timeout_s)
{
    (void)a; (void)url; (void)token; (void)timeout_s;
    fprintf(stderr, "sync: this build lacks LAN sync support (needs POSIX + the crypto module)\n");
    return -1;
}

#endif
