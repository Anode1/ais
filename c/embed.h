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

/* Recall records under space-separated KEYS. or_mode: 0 = AND (intersection),
 * non-zero = OR (union). Returns a newly allocated, NUL-terminated buffer of
 * "id|value\n" lines (empty string if no matches); free it with
 * ais_embed_free(). Returns NULL only on bad arguments or allocation failure. */
char *ais_embed_recall(void *handle, const char *keys, int or_mode);

/* Store VALUE under KEYS. Returns the record id (> 0), or -1 on error. */
long  ais_embed_store(void *handle, const char *keys, const char *value);

/* The LIMIT most-recent records as "id|ts|keys|value\n" lines (LIMIT <= 0 =
 * default cap), ordered for a timeline: dateless first, then newest. Free with
 * ais_embed_free(). NULL only on bad args / allocation failure. */
char *ais_embed_timeline(void *handle, int limit);

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
