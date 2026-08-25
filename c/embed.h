/* embed.h -- in-process API for EMBEDDERS (Flutter dart:ffi, a native mobile
 * plugin, any host that links the engine instead of shelling out to the CLI).
 *
 * The same engine as the CLI and `ais serve`: recall returns the exact
 * "id|value\n" text the web /api/get returns, so every front-end shares one
 * contract. Handles are void*: an opaque pointer (Dart Pointer<Void>), no struct
 * layout to track across the FFI boundary.
 *
 *   void *h = ais_embed_open("/path/to/index");   // NULL on failure
 *   char *r = ais_embed_recall(h, "venice italy", 0);  // 0 = AND, 1 = OR
 *   ...                                            // r is "id|value\n"...
 *   ais_embed_free(r);
 *   ais_embed_store(h, "venice italy", "https://example.org/p");
 *   ais_embed_close(h);
 */
/* The rules a signature cannot state, and every embedder needs:
 *
 * - NOTHING HERE BELONGS ON THE UI THREAD. ais_embed_sync_serve blocks for up to
 *   its timeout (~120 s) waiting for a peer, and ais_embed_sync_pull blocks for
 *   the transfer. The Flutter app runs them on a background isolate.
 * - ONE CALLER PER HANDLE. The handle is single-writer, so a recall issued while
 *   a sync runs on the same handle is a data race. Calls serialize onto one
 *   queue.
 * - ONE SYNC AT A TIME. A scanned pairing link can arrive while a sync is
 *   already running; the Flutter app keeps a _syncBusy flag and refuses the
 *   second.
 * - ONE LONG-LIVED HANDLE PER INDEX. ais_embed_open holds the single-writer lock
 *   for the handle's lifetime, so the same directory is never opened twice.
 * - SIGPIPE from a dropped socket is ignored inside this layer, so a peer
 *   hanging up mid-sync does not terminate the host process. There is no fork()
 *   on this path (that lives only in the CLI and the web host), which is what
 *   makes it usable on iOS.
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

/* Keyset page of ais_embed_recall, for scrolling a large match set: emit the
 * COUNT records with id > AFTER (AFTER <= 0 from the first, COUNT <= 0 = whole
 * set). Ids ascend, so the host reads the largest id in the page and passes it
 * back as AFTER for the next page. Same "id|value\n" format; free with
 * ais_embed_free(). NULL on bad args / OOM. */
char *ais_embed_recall_page(void *handle, const char *keys, int or_mode,
                            long after, int count);

/* Content search: recall records whose VALUE contains NEEDLE (a plain, case-
 * sensitive substring). Same "id|value\n" line format as ais_embed_recall.
 * Returns a newly allocated, NUL-terminated buffer (empty string if nothing
 * matches); free with ais_embed_free(). NULL on bad args / OOM. */
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
 * (wrong passphrase, not an inline secret, or crypto not built). Encrypted
 * DOCUMENTS (aisc:@blob) are revealed via the CLI, not here. */
char *ais_embed_reveal(const char *marked_value, const char *passphrase);

/* Delete record ID (the id is the "id|value" handle from recall/timeline).
 * Returns 0 on success, -1 on error. */
int   ais_embed_del(void *handle, long id);

/* Edit record ID's keys: each bare token in KEYS attaches, each "-key" detaches.
 * The id and value are unchanged. Returns 0, or -1 if id is unknown/deleted. */
int   ais_embed_update(void *handle, long id, const char *keys);

/* Replace record ID's VALUE (OLD_VALUE -> NEW_VALUE), preserving its id, ts and
 * keys. OLD_VALUE must be the record's exact stored value (the "id|value" handle
 * from recall/timeline). Returns 0, or -1 if the id is unknown, the value does
 * not match (nothing is changed), or on IO error. */
int   ais_embed_set_value(void *handle, long id, const char *old_value,
                          const char *new_value);

/* Doc-aware ais_embed_set_value: TEXT may span lines, and the representation is
 * chosen at this write as saving does (multi-line to a fresh blob, one line
 * inline). OLD_VALUE is the stored string; for a document that is its "blobs/"
 * path. On success NEWVAL (when non-NULL, NVSZ bytes) gets the value the
 * record now stores -- the trimmed line or the fresh blob's path. Same return
 * codes, plus -2/-3 when a live/deleted record already holds TEXT. */
int   ais_embed_set_value_text(void *handle, long id, const char *old_value,
                               const char *text, char *newval, int nvsz);

/* The WHOLE document behind a "blobs/" VALUE, for an editor to start from
 * (ais_embed_display is a bounded preview and may cut the tail). Freshly
 * allocated, free with ais_embed_free(). NULL when VALUE is not a document
 * blob present on this device, is not editable as text (NUL / non-UTF-8, see
 * ais_doc_text_ok), or exceeds AIS_EMBED_DOC_EDIT_MAX. */
char *ais_embed_doc_read(void *handle, const char *value);

/* The largest document ais_embed_doc_read hands to an editor: bigger bodies
 * stay view-only (three in-memory copies ride every edit on a phone). */
#define AIS_EMBED_DOC_EDIT_MAX (1024 * 1024)

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
 * one round (no sender/receiver role). One device hosts (sync_serve), the other
 * joins (sync_pull); either way both end up merged.
 *
 * Same return codes, plus two POSITIVE ones. Neither is a failure: nothing was
 * lost, and running the sync again finishes the job.
 *   1 = HALF DONE -- one direction completed and the other did not, so records
 *       crossed but the two devices are not identical yet.
 *   2 = RUN IT AGAIN -- both directions completed, but a record here outlived a
 *       delete that arrived in this same round, decided after our own stream had
 *       gone out. The peer still has the record deleted; one more exchange
 *       settles it. */
int   ais_embed_sync_pull(void *handle, const char *url, const char *token);
int   ais_embed_sync_serve(void *handle, int port, const char *token);

/* File bundle (offline sync): write the WHOLE index to PATH as a PLAINTEXT bundle
 * (no passphrase, no AEAD), to move by any channel (Drive / USB / email) and import
 * elsewhere -- covering PC<->PC and Windows, which live LAN sync can't. Encrypted
 * VALUES stay opaque (already "aisc:" ciphertext in the store, carried as-is).
 * File I/O only, no sockets. Returns 0, or -1 (bad args / write). */
int   ais_embed_export_bundle(void *handle, const char *path);

/* Read the plaintext bundle at PATH (capped at AIS_SYNC_MAX_BLOB) and merge it into
 * the index -- the same tombstone-union last-writer-wins as socket sync. Returns 0
 * (merged), -1 (I/O / bad args / malformed), or -2 (version mismatch: an older/newer
 * bundle format), so a GUI can tell "unreadable/wrong file" from "wrong format". */
int   ais_embed_import_bundle(void *handle, const char *path);

/* Folder auto-sync: one export+import pass over a shared FOLDER (a Syncthing / cloud
 * folder). Each device owns a framed <id>.aisb; the merge is conflict-free and a
 * torn/partial peer file is rejected.
 *
 * Returns 0, or one of the AIS_FOLDER_* codes (sync.h): -2 no such folder, -3 not a
 * folder, -4 unreadable, -5 we have synced here before and our own bundle is gone
 * (an unmounted drive, an emptied share), -6 the merge applied but our bundle could
 * not be written; -1 anything else. A front end must SHOW which -- the remedies
 * differ. FORCE (0/1) accepts the -5 folder and re-establishes it. Creates nothing. */
int   ais_embed_sync_folder(void *handle, const char *folder);
int   ais_embed_sync_folder_force(void *handle, const char *folder, int force);

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

/* Keyset page of ais_embed_tags, for scrolling a large tag cloud. Emit up to
 * COUNT tags after the (AFTER_COUNT, AFTER_KEY) cursor in busiest-first order.
 * AFTER_KEY "" (or NULL) is the first page; the host reads the last "count|key"
 * row and passes both back for the next page. COUNT <= 0 = whole cloud. Free
 * with ais_embed_free(). NULL only on bad args / allocation failure. */
char *ais_embed_tags_page(void *handle, long after_count, const char *after_key,
                          int count);

/* Record ID's keys as one space-separated string (the same KEYS field the
 * timeline emits). Free with ais_embed_free(). Returns "" (empty, not NULL) if
 * the record has no keys or ID is unknown/deleted; NULL only on bad args /
 * allocation failure. */
char *ais_embed_keys(void *handle, long id);

/* Resolve VALUE to the bounded text a GUI SHOWS (see ais_doc_display): a document
 * blob's CONTENT, else VALUE verbatim. Returns a freshly-allocated, NUL-terminated
 * string (free with ais_embed_free), or NULL on bad args / OOM. Hosts call this
 * instead of reading blob files, so blob resolution stays in one place. */
char *ais_embed_display(void *handle, const char *value);

/* Free a buffer returned by ais_embed_recall() / _timeline() / _tags() / _display(). */
void  ais_embed_free(char *buf);

/* How many LIVE records the index holds -- the count a front-end shows after a
 * sync, which is the only confirmation that anything arrived. Returns the count
 * (>= 0), or -1 on bad args / read error. */
long  ais_embed_count(void *handle);

/* Release the lock, flush the id counter, free the handle. */
void  ais_embed_close(void *handle);

/* Persist DIR as the saved default index in ~/.ais/config (for a GUI's "change
 * store"), so the next run opens it. 0 on success, -1 on failure. */
int   ais_embed_default_set(const char *dir);

/* Reclaim the space of deleted records and rebuild the posting index. FORGET=0
 * keeps the deletions exportable, which is what a peer needs to learn about them;
 * FORGET=1 also strips the content hash from every tombstone, so the deletions
 * stay in force HERE but stop travelling and stop being testable against a guess.
 * A phone has no CLI, so without this the store grows forever and tags of deleted
 * records linger in the index. Safe to run unattended: atomic, and an interrupted
 * run is rolled back at the next open. With FORGET=1 warn first -- a device that
 * has not synced since can push the deleted records back. 0/-1. */
int   ais_embed_compact(void *handle, int forget);

/* Resolve the index a bare run would use (same precedence as the CLI: nearest
 * .ais/, then ~/.ais/config, then ~/.ais), writing it into OUT (size OUTSZ).
 * 0 on success, -1 on error. */
int   ais_embed_locate(char *out, size_t outsz);

#endif /* AIS_EMBED_H */
