/* doc.h -- store a document (a multi-line value) as a blob file under
 * <index>/blobs/ and put its relative path as a record value. Shared by the
 * CLI (feed_doc), the web server (serve.c), and the FFI seam (embed.c) so the
 * rule "a multi-line paste becomes ONE blob-backed record, never one record
 * per line" is decided in a single place.
 *
 * Unlike feed.c, these are die()-free: every failure returns -1, never exits.
 * A server and a linked library must not call exit() on a write error. */
#ifndef AIS_DOC_H
#define AIS_DOC_H

#include <stddef.h>
#include "ais.h"

/* Ensure <index>/blobs/ exists and pick a free, timestamp-named blob path.
 * Fills RELVAL ("blobs/<ts>.txt", the stored value) and the absolute BLOBPATH.
 * Returns 0 on success, -1 on error. */
int  ais_doc_blobname(const ais *a, char *relval, size_t rvsz,
                      char *blobpath, size_t bpsz);

/* Write CONTENT (LEN bytes) to a new blob and put its path under KEYS.
 * Returns the new record id, or -1 on any failure. */
long ais_doc_put(ais *a, const char *keys, const char *content, size_t len);

/* Store VALUE as exactly ONE record: a plain put when it is a single line, or
 * a blob-backed document when it has an interior newline. A lone trailing
 * newline does not count (a one-line paste stays a plain record). Returns the
 * new record id, or -1. This is the entry point every GUI calls. */
long ais_put_value(ais *a, const char *keys, const char *value);

#endif /* AIS_DOC_H */
