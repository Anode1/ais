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
#endif

#ifdef SYNC_HAVE

int sync_export_sealed(ais *a, const char *token, uint8_t **out, size_t *out_len)
{
    char *buf = NULL;
    size_t blen = 0;
    FILE *ms;
    int rc;

    if (!a || !token || !out || !out_len)
        return -1;
    ms = open_memstream(&buf, &blen);          /* capture the merge stream to memory */
    if (ms == NULL)
        return -1;
    feed_export(a, ms);
    if (fclose(ms) != 0) { free(buf); return -1; }

    rc = aisc_seal((const uint8_t *)token, strlen(token),
                   (const uint8_t *)buf, blen, out, out_len);
    if (blen)
        aisc_wipe(buf, blen);                  /* the cleartext export held secrets */
    free(buf);
    return (rc == AISC_OK) ? 0 : -1;
}

int sync_import_sealed(ais *a, const char *token, const uint8_t *sealed, size_t len)
{
    uint8_t *plain = NULL;
    size_t plen = 0;
    FILE *mf;

    if (!a || !token || !sealed)
        return -1;
    /* authenticate FIRST: a wrong token or any tampering fails here, before any merge */
    if (aisc_unseal((const uint8_t *)token, strlen(token), sealed, len, &plain, &plen) != AISC_OK)
        return -1;

    mf = fmemopen(plain, plen, "r");
    if (mf == NULL) { aisc_wipe(plain, plen); free(plain); return -1; }
    feed_import_from(a, mf);                    /* unsealed stream -> merge */
    fclose(mf);
    aisc_wipe(plain, plen);
    free(plain);
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

#endif
