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

/* Free a buffer returned by ais_embed_recall() / _timeline() / _tags(). */
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
