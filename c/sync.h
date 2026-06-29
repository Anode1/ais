/* sync.h -- LAN sync transport endpoints.
 *
 * The merge engine (feed_export / feed_import_from) and the AEAD (ais_crypto) do the
 * work; these two functions glue them: seal an index's merge stream under a one-time
 * token, and apply a received sealed stream by unsealing then merging. The socket
 * transport (`ais --export --serve` / `ais --import <url>`) only moves the sealed blob
 * between two devices on the same LAN; it calls these. POSIX + crypto only.
 */
#ifndef AIS_SYNC_H
#define AIS_SYNC_H

#include <stddef.h>
#include <stdint.h>
#include "ais.h"

/* Produce A's merge stream (A|/D| lines) sealed under TOKEN, a high-entropy one-time
 * pairing secret. Allocates *OUT (caller frees; wipe with aisc_wipe). Returns 0, or -1
 * (incl. when the build lacks POSIX buffer streams or the crypto module). */
int sync_export_sealed(ais *a, const char *token, uint8_t **out, size_t *out_len);

/* Unseal a received SEALED blob (LEN bytes) with TOKEN and merge it into A under
 * last-write-wins. A wrong token or any tampering fails (-1) BEFORE anything is merged,
 * so unauthenticated bytes never reach the store. Returns 0, or -1. */
int sync_import_sealed(ais *a, const char *token, const uint8_t *sealed, size_t len);

/* Serve ONE peer over the LAN: bind PORT, accept a client, check its TOKEN, then send the
 * sealed merge stream and exit (single-shot, ephemeral). TIMEOUT_S bounds the wait. 0 on a
 * served client, -1 on error/timeout/auth failure. POSIX + crypto only. */
int sync_serve(ais *a, int port, const char *token, int timeout_s);

/* Pull from a peer at HOST:PORT: send TOKEN, receive the sealed stream, unseal + merge.
 * TIMEOUT_S bounds I/O. 0, or -1 on error/timeout/auth failure. */
int sync_pull(ais *a, const char *host, int port, const char *token, int timeout_s);

#endif /* AIS_SYNC_H */
