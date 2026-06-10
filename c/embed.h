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

/* Free a buffer returned by ais_embed_recall(). */
void  ais_embed_free(char *buf);

/* Release the lock, flush the id counter, free the handle. */
void  ais_embed_close(void *handle);

#endif /* AIS_EMBED_H */
