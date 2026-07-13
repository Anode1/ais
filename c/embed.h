/* embed.h -- in-process API for EMBEDDERS (Flutter dart:ffi, a native mobile
 * plugin, any host that links the engine instead of shelling out to the CLI).
 *
 * It is the same engine as the CLI and `ais serve`: recall returns the exact
 * "id|value\n" text the web /api/get returns, so every front-end shares one
 * contract. Handles are void* on purpose -- the host sees an opaque pointer
 * (Dart Pointer<Void>), with no struct layout to track across the FFI boundary.
 *
 *   void *h = ais_embed_open("/path/to/index");   // NULL on failure
 *   char *r = ais_embed_recall(h, "venice italy", 0);  // 0 = AND, 1 = OR
 *   ...                                            // r is "id|value\n"...
 *   ais_embed_free(r);
 *   ais_embed_store(h, "venice italy", "https://example.org/p");
 *   ais_embed_close(h);
 */
#ifndef AIS_EMBED_H
#define AIS_EMBED_H

#include <stddef.h>     /* size_t */

/* Open (creating if absent) the index directory; takes the single-writer lock
 * for the life of the handle. Returns an opaque handle, or NULL on failure. */
void *ais_embed_open(const char *dir);

/* Recall records under space-separated KEYS. or_mode is the mode switch:
 * 0 = AND (intersection), non-zero = OR (union); no automatic relaxation.
 * Returns a newly allocated, NUL-terminated buffer of "id|value\n" lines (empty
 * string if no matches); free with ais_embed_free(). NULL on bad args / OOM. */
char *ais_embed_recall(void *handle, const char *keys, int or_mode);

/* Content search: recall records whose VALUE contains NEEDLE (a plain, case-
 * sensitive substring), for a "forgot the key" fallback. Same "id|value\n" line
 * format as ais_embed_recall, so the host reuses one parser. Returns a newly
 * allocated, NUL-terminated buffer (empty string if nothing matches); free with
 * ais_embed_free(). NULL on bad args / OOM. */
char *ais_embed_find(void *handle, const char *needle);

/* Store VALUE under KEYS. Returns the record id (> 0), or -1 on error. */
long  ais_embed_store(void *handle, const char *keys, const char *value);

/* Store VALUE under KEYS, ENCRYPTED under PASSPHRASE (the "aisc:" marker), for a
 * GUI's "encrypt" toggle. Returns the record id (> 0), or -1 (error, or the
 * crypto module is not built). PASSPHRASE is used, not retained. */
long  ais_embed_store_encrypted(void *handle, const char *keys,
                                const char *value, const char *passphrase);

/* Decrypt a marked ("aisc:") inline VALUE under PASSPHRASE, returning the
 * cleartext as a freshly-allocated string (free with ais_embed_free), or NULL
 * (wrong passphrase, not an inline secret, or crypto not built). For a GUI's
 * reveal; encrypted DOCUMENTS (aisc:@blob) are revealed via the CLI. */
char *ais_embed_reveal(const char *marked_value, const char *passphrase);

/* Delete record ID (the id is the "id|value" handle from recall/timeline).
 * Returns 0 on success, -1 on error. */
int   ais_embed_del(void *handle, long id);

/* Edit record ID's keys: each bare token in KEYS attaches, each "-key" detaches.
 * The id and value are unchanged. Returns 0, or -1 if id is unknown/deleted. */
int   ais_embed_update(void *handle, long id, const char *keys);

/* Replace record ID's VALUE (OLD_VALUE -> NEW_VALUE), preserving its id, ts and
 * keys -- the in-place value edit, for a GUI's "edit value". OLD_VALUE must be
 * the record's exact stored value (the "id|value" handle from recall/timeline).
 * Returns 0, or -1 if the id is unknown, the value does not match (nothing is
 * changed), or on IO error. */
int   ais_embed_set_value(void *handle, long id, const char *old_value,
                          const char *new_value);

/* Sync (Receive): pull + merge a peer's `ais --export --serve` over the LAN,
 * last-writer-wins by timestamp. URL is http://host[:port]; TOKEN is the peer's
 * one-time token. Returns 0 (merged), -1 (bad args / malformed URL), or -2
 * (could not connect, wrong token, or timeout). Does not print. */
int   ais_embed_pull(void *handle, const char *url, const char *token);

/* Sync (Send): serve this index to ONE LAN peer that pulls with
 * `ais --import <url> --token TOKEN`. Single-shot: blocks up to an internal
 * timeout waiting for one peer, then returns (run it off the host's UI thread).
 * TOKEN is a shared secret the caller shows the user; the peer must supply the
 * same. Returns 0 (a peer pulled and merged), -1 (bad args), -2 (no peer
 * completed: timeout, wrong token, or error), or -3 (the port is busy: bind
 * failed, returned at once, not after the timeout). Does not print. */
int   ais_embed_serve(void *handle, int port, const char *token);

/* The direction-less "Sync": like pull/serve, but a SYMMETRIC exchange -- after
 * the one-way transfer each side also sends the other way, so BOTH converge in
 * one round (no sender/receiver role). Same return codes. One device hosts
 * (sync_serve), the other joins (sync_pull); either way both end up merged. */
int   ais_embed_sync_pull(void *handle, const char *url, const char *token);
int   ais_embed_sync_serve(void *handle, int port, const char *token);

/* File bundle (offline sync): write the WHOLE index to PATH as a PLAINTEXT bundle
 * (no passphrase, no AEAD), for the user to save/move by any channel (Drive / USB /
 * email) and import elsewhere -- covering PC<->PC and Windows, which live LAN sync
 * can't. Encrypted VALUES stay opaque (they are already "aisc:" ciphertext in the
 * store, carried as-is). File I/O only, no sockets. Returns 0, or -1 (bad args /
 * write). */
int   ais_embed_export_bundle(void *handle, const char *path);

/* Read the plaintext bundle at PATH (capped at AIS_SYNC_MAX_BLOB) and merge it into
 * the index -- the same tombstone-union last-writer-wins as socket sync. Returns 0
 * (merged), -1 (I/O / bad args / malformed), or -2 (version mismatch: an older/newer
 * bundle format), so a GUI can tell "unreadable/wrong file" from "wrong format". */
int   ais_embed_import_bundle(void *handle, const char *path);

/* Folder auto-sync: one export+import pass over a shared FOLDER (a Syncthing / cloud
 * folder). Each device owns a framed <id>.aisb; the merge is conflict-free and a
 * torn/partial peer file is rejected. Returns 0, or -1 (bad args / not built). */
int   ais_embed_sync_folder(void *handle, const char *folder);

/* One timeline page as "id|ts|keys|value\n" lines: the COUNT records with id <
 * BEFORE_ID (BEFORE_ID <= 0 = from newest; COUNT <= 0 = default), newest first,
 * whose save date is within [FROM,TO] ("YYYY-MM-DD", inclusive; "" / NULL = open
 * end). Keyset paging -- "load more" passes the last id of the previous page as
 * BEFORE_ID (FROM/TO held). Free with ais_embed_free(); NULL only on bad args. */
char *ais_embed_timeline(void *handle, long before_id, int count,
                         const char *from, const char *to);

/* Every distinct key as "count|key\n" lines, busiest first. Free with
 * ais_embed_free(). NULL only on bad args / allocation failure. */
char *ais_embed_tags(void *handle);

/* Record ID's keys as one space-separated string (the same KEYS field the
 * timeline emits), for a GUI's "edit keys" chip editor. Free with
 * ais_embed_free(). Returns "" (empty, not NULL) if the record has no keys or
 * ID is unknown/deleted; NULL only on bad args / allocation failure. */
char *ais_embed_keys(void *handle, long id);

/* Resolve VALUE to the bounded text a GUI SHOWS (see ais_doc_display): a document
 * blob's CONTENT, else VALUE verbatim. Returns a freshly-allocated, NUL-terminated
 * string (free with ais_embed_free), or NULL on bad args / OOM. The Flutter app
 * calls this instead of reading blob files itself, so blob resolution lives in
 * ONE place (this engine), shared with `ais serve` -- no viewer can drift. */
char *ais_embed_display(void *handle, const char *value);

/* Free a buffer returned by ais_embed_recall() / _timeline() / _tags() / _display(). */
void  ais_embed_free(char *buf);

/* Release the lock, flush the id counter, free the handle. */
void  ais_embed_close(void *handle);

/* Persist DIR as the saved default index in ~/.ais/config (for a GUI's "change
 * store"), so the next run opens it. 0 on success, -1 on failure. */
int   ais_embed_default_set(const char *dir);

/* Resolve the index a bare run would use (same precedence as the CLI: nearest
 * .ais/, then ~/.ais/config, then ~/.ais), writing it into OUT (size OUTSZ).
 * 0 on success, -1 on error. Lets an embedder open the same index as the CLI
 * without duplicating the resolution logic. */
int   ais_embed_locate(char *out, size_t outsz);

#endif /* AIS_EMBED_H */
