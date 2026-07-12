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

#define AIS_SYNC_PORT 8766       /* default LAN sync port (distinct from --serve's 8765 GUI) */
#define AIS_SYNC_MAX_BLOB (64u * 1024u * 1024u)   /* cap on one sealed transfer, BOTH sides */

/* Assemble A's raw (UNSEALED) bundle -- version byte + blob sections + merge stream --
 * the shared core the file bundle (plaintext) and LAN sync (which seals this) both use.
 * Allocates *OUT (caller frees). Enforces the same size cap as the wire. Returns 0, or -1
 * (incl. when the build lacks POSIX buffer streams or the crypto module). */
int sync_export_plain(ais *a, uint8_t **out, size_t *out_len);

/* Parse + merge a raw (UNSEALED) bundle DATA[0..len): version gate, blob-import loop,
 * then merge the record text (last-write-wins). DATA is owned by the caller. Returns 0,
 * -1 (bad args / malformed / I/O), or -2 (unrecognized version byte -- a LOUD failure). */
int sync_import_plain(ais *a, const uint8_t *data, size_t len);

/* Produce A's merge stream (A|/D| lines) sealed under TOKEN, a high-entropy one-time
 * pairing secret. Allocates *OUT (caller frees; wipe with aisc_wipe). Returns 0, or -1
 * (incl. when the build lacks POSIX buffer streams or the crypto module). */
int sync_export_sealed(ais *a, const char *token, uint8_t **out, size_t *out_len);

/* Unseal a received SEALED blob (LEN bytes) with TOKEN and merge it into A under
 * last-write-wins. A wrong token or any tampering fails (-1) BEFORE anything is merged,
 * so unauthenticated bytes never reach the store. Returns 0, or -1. */
int sync_import_sealed(ais *a, const char *token, const uint8_t *sealed, size_t len);

/* Serve ONE peer over the LAN: bind PORT, accept a client, check its TOKEN, then send the
 * sealed merge stream and exit (single-shot, ephemeral). TIMEOUT_S bounds the wait. If
 * BIDIR, after sending it also receives and merges the peer's sealed stream (a symmetric
 * full-state exchange -- both converge in one round, no sender/receiver role). 0 on a
 * served client, -1 on error/timeout/auth failure. POSIX + crypto only. */
int sync_serve(ais *a, int port, const char *token, int timeout_s, int bidir);

/* Pull from a peer at HOST:PORT: send TOKEN, receive the sealed stream, unseal + merge.
 * If BIDIR, after merging it also seals and sends its own stream back so the peer
 * converges too. TIMEOUT_S bounds I/O. 0, or -1 on error/timeout/auth failure. */
int sync_pull(ais *a, const char *host, int port, const char *token, int timeout_s, int bidir);

/* High-level CLI wrappers (these also generate the token and print the pairing line). */

/* Generate a one-time token, print the pairing line (URL + token) for the peer, then serve
 * ONE pull over the LAN on PORT for up to TIMEOUT_S. If BIDIR, the exchange is symmetric
 * (both converge) and the printed pairing line is `ais --sync` rather than `ais --import`.
 * 0 on a served peer, -1 otherwise. */
int sync_serve_lan(ais *a, int port, int timeout_s, int bidir);

/* Parse URL (`http://host:port` or `host:port`; default port AIS_SYNC_PORT) and pull from
 * it with TOKEN, merging into A. If BIDIR, also sends A's stream back so the peer converges.
 * 0 on success, -1 otherwise. */
int sync_pull_url(ais *a, const char *url, const char *token, int timeout_s, int bidir);

/* Parse a sync URL into HOST (bounded by HOSTSZ) and *PORT: "http(s)://host[:port][/path]"
 * or "host[:port]"; a missing or out-of-range port defaults to AIS_SYNC_PORT. Pure string
 * logic (no sockets/crypto), always compiled. 0 on success, -1 if the host is empty. */
int sync_parse_url(const char *url, char *host, size_t hostsz, int *port);

#endif /* AIS_SYNC_H */
