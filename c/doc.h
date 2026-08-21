/* doc.h -- store a document (a multi-line value) as a blob file under
 * <index>/blobs/ and put its relative path as a record value. Shared by the
 * CLI (feed_doc), the web server (serve.c) and the FFI seam (embed.c): a
 * multi-line paste becomes one blob-backed record, never one record per line.
 *
 * die()-free: every failure returns -1. A server and a linked library must not
 * call exit() on a write error. */
#ifndef AIS_DOC_H
#define AIS_DOC_H

#include <stddef.h>
#include "ais.h"

/* Ensure <index>/blobs/ exists and pick a free, timestamp-named blob path with
 * the given extension (no dot). Fills RELVAL ("blobs/<ts>.<ext>", the stored
 * value) and the absolute BLOBPATH. Returns 0 on success, -1 on error. */
int  ais_doc_blobname_ext(const ais *a, const char *ext, char *relval, size_t rvsz,
                          char *blobpath, size_t bpsz);

/* As above with the default "txt" extension (a plain-text document blob). */
int  ais_doc_blobname(const ais *a, char *relval, size_t rvsz,
                      char *blobpath, size_t bpsz);

/* Place an arriving document body, already written to TMPPATH, under
 * <dir>/blobs/ and report in OUTREL the relative value its record should carry.
 * An existing file is never overwritten: an identical body is dropped, and a
 * DIFFERENT body whose name is taken lands under a name derived from its own
 * bytes, so two devices resolving one clash agree without talking. TMPPATH is
 * consumed. Returns 0, or -1 (TMPPATH is then the caller's to remove). */
int  ais_doc_blob_place(const char *dir, const char *rel, const char *tmppath,
                        char *outrel, size_t osz);

/* The arriving-name -> local-name map an import builds while placing bodies. It
 * is applied ONCE per record value, so a value can never be rewritten twice. */
typedef struct { char **from; char **to; int n, cap; } ais_blobmap;
int         ais_blobmap_add(ais_blobmap *m, const char *from, const char *to);
const char *ais_blobmap_get(const ais_blobmap *m, const char *from);
void        ais_blobmap_free(ais_blobmap *m);

/* Write CONTENT (LEN bytes) to a new blob and put its path under KEYS.
 * Returns the new record id, or -1 on any failure. */
long ais_doc_put(ais *a, const char *keys, const char *content, size_t len);

/* Store VALUE as exactly ONE record: a plain put when it is a single line, or
 * a blob-backed document when it has an interior newline. A lone trailing
 * newline does not count (a one-line paste stays a plain record). Returns the
 * new record id, or -1. This is the entry point every GUI calls. */
long ais_put_value(ais *a, const char *keys, const char *value);

/* Is VALUE a document-blob reference ("blobs/<...>", the out-of-line store for a
 * multi-line value)? If so, build the blob's absolute path under the index in
 * PATH and return 1; else 0. Existence is the caller's to check (open PATH,
 * show VALUE if it fails). */
int  ais_doc_is_blob(const ais *a, const char *value, char *path, size_t psz);

/* Discard the file backing VALUE when the INDEX owns it, and only then: a
 * document blob under blobs/ is removed, an encrypted blob is shredded
 * (secret_shred_blob). A value that merely POINTS somewhere -- a path anywhere
 * else on disk, a URL, inline text -- is left untouched, because that is the
 * product: ais indexes your things where they are and never holds them hostage.
 * The two cases differ in who made the file, not in how it looks.
 *
 * Call it while the value is still readable: before the tombstone on delete,
 * after the store no longer points at it on replace, and when compaction drops
 * a deleted line. Takes the index dir, not an `ais`, so a value callback can
 * pass the one it already carries. */
void ais_doc_discard(const char *index_dir, const char *value);

/* ais_doc_discard in ais_discard_cb shape: pass it to ais_on_discard with the
 * index dir as ctx, so a delete arriving from a peer disposes of the payload
 * here exactly as a local delete does. */
void ais_doc_discard_cb(const char *value, void *index_dir);

/* Resolve VALUE to the bounded preview a GUI should display; the web server and
 * the Flutter app share this one implementation (the CLI cats the full blob
 * instead). If VALUE is a document blob, read its content into OUT, capped to
 * OUTSZ-1 bytes with a trailing "…" when truncated, and return the byte length
 * (>= 0). For anything else -- inline text, a URL, a path, an "aisc:" secret,
 * an absent or unreadable blob -- copy VALUE verbatim into OUT and return -1,
 * so a caller can tell it was not a resolved document. OUT is always
 * NUL-terminated unless OUTSZ == 0. */
long ais_doc_display(const ais *a, const char *value, char *out, size_t outsz);

#endif /* AIS_DOC_H */
