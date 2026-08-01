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

/* HALF a bidirectional exchange succeeded: THIS device merged the peer's records
 * (so nothing was lost and the data is here), but the other direction did not
 * complete, so the peer has not got ours. Distinct from -1 because the two need
 * opposite advice: -1 means try again, this means try again to finish copying
 * the other way -- and reporting it as a plain failure told a user their backup
 * had not happened when in fact half of it had. Only ever returned when BIDIR. */
#define AIS_SYNC_PARTIAL 1

/* Both directions completed, but the two devices are still NOT identical: this
 * round merged a delete that a local edit beat, and the news of that survival
 * (`sts`) was decided after our own stream had already been sent. The peer
 * therefore still has the record deleted, and one more exchange settles it.
 *
 * A round that ends this way was reported as a plain success, under the words
 * "both devices now have the same records" -- which was simply untrue, and for a
 * feature that stands in for a backup that is the one thing not to be wrong
 * about. Positive, like AIS_SYNC_PARTIAL: nothing failed and nothing was lost. */
#define AIS_SYNC_AGAIN 2

/* Serve ONE peer over the LAN: bind PORT, accept a client, check its TOKEN, then send the
 * sealed merge stream and exit (single-shot, ephemeral). TIMEOUT_S bounds the wait. If
 * BIDIR, after sending it also receives and merges the peer's sealed stream (a symmetric
 * full-state exchange -- both converge in one round, no sender/receiver role). 0 on a
 * fully converged exchange, AIS_SYNC_PARTIAL when the peer got our records but we
 * did not get theirs, -1 on error/timeout/auth failure. POSIX + crypto only. */
int sync_serve(ais *a, int port, const char *token, int timeout_s, int bidir);

/* Pull from a peer at HOST:PORT: send TOKEN, receive the sealed stream, unseal + merge.
 * If BIDIR, after merging it also seals and sends its own stream back so the peer
 * converges too. TIMEOUT_S bounds I/O. 0 when both converged, AIS_SYNC_PARTIAL when
 * we merged theirs but could not send ours back, -1 on error/timeout/auth failure. */
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

/* ----- folder auto-sync: a framed per-device bundle in a shared folder (Syncthing /
 * cloud folder). The merge is conflict-free; these add the file-level protocol. ----- */

/* Load (or generate+persist on first use) this device's sync identity from
 * <index>/syncid. ID_HEX must hold >= 17 bytes (16 hex + NUL); NONCE is 16 bytes.
 * 0 on success, -1 on error. sync_device_new forces a FRESH identity (clone heal). */
int sync_device_id(ais *a, char *id_hex, size_t idsz, uint8_t nonce[16]);
int sync_device_new(ais *a, char *id_hex, size_t idsz, uint8_t nonce[16]);

/* Wrap sync_export_plain in a self-describing FILE frame (magic+ver+nonce+seq+len+crc32)
 * so a torn/partial/corrupt read is rejected wholesale. Allocates *OUT (caller frees). */
int sync_export_framed(ais *a, const uint8_t nonce[16], uint64_t seq,
                       uint8_t **out, size_t *out_len);

/* The first byte of a plain bundle: the container version. A file WITHOUT it is
 * a bare merge stream (what `ais --export` and `--dump` write) and is accepted
 * too -- see sync_import_plain. */
#define AIS_SYNC_PROTO 1

/* Verify a framed bundle and merge it. -1 = short/truncated/corrupt (REJECTED before
 * any merge), -2 = unknown frame version, 0 = merged. NONCE_OUT / SEQ_OUT (optional)
 * get the writer's nonce and write-sequence. DATA owned by caller. */
int sync_import_framed(ais *a, const uint8_t *data, size_t len,
                       uint8_t nonce_out[16], uint64_t *seq_out);

/* One folder-sync pass: import every well-formed peer <id>.aisb in FOLDER, then write
 * our own atomically; heals a device-id clone (nonce mismatch).
 *
 * The folder is never CREATED, and a folder we have synced with before is never
 * silently accepted once it holds no device bundle at all. Both refusals exist
 * because sync IS the backup here: a pass that reports success while writing into
 * a typo, an unmounted drive, or an emptied share is the one failure the user
 * cannot detect until the data is needed.
 *
 *   0                       synced
 *   AIS_FOLDER_MISSING      no such folder (or a dangling symlink)
 *   AIS_FOLDER_NOTDIR       the path exists but is not a directory
 *   AIS_FOLDER_STAT         it could not be examined; errno says why
 *   AIS_FOLDER_STRANGER     we synced here before and it now holds no device
 *                           bundle at all: an unmounted drive, or an emptied share
 *   AIS_FOLDER_NOWRITE      imports may have applied, but our bundle could not be written
 *   -1                      any other failure
 *
 * FORCE (the -y path) accepts an empty remembered folder and re-establishes it; it
 * does not create anything and does not bypass the other checks.
 *
 * Known limit: a folder is identified by its PATH, never by the filesystem behind
 * it. The very first pass into an unmounted mount point therefore succeeds and
 * writes into the underlying directory. */
#define AIS_FOLDER_MISSING   (-2)
#define AIS_FOLDER_NOTDIR    (-3)
#define AIS_FOLDER_STAT      (-4)
#define AIS_FOLDER_STRANGER  (-5)
#define AIS_FOLDER_NOWRITE   (-6)

int sync_folder_once(ais *a, const char *folder);
int sync_folder_once_force(ais *a, const char *folder, int force);

/* Parse a sync URL into HOST (bounded by HOSTSZ) and *PORT: "http(s)://host[:port][/path]"
 * or "host[:port]"; a missing or out-of-range port defaults to AIS_SYNC_PORT. Pure string
 * logic (no sockets/crypto), always compiled. 0 on success, -1 if the host is empty. */
int sync_parse_url(const char *url, char *host, size_t hostsz, int *port);

#endif /* AIS_SYNC_H */
