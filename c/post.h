/* post.h -- per-key posting lists, sharded and navigable.
 *
 * A key's posting list lives at idx/<p>/<key> (see key.h for <p>): its record
 * ids, one per line, ascending. Because ids are monotonic and put only appends
 * the newest (largest) id, the file stays sorted with no sort step -- which is
 * what makes get a pure merge.
 *
 * A posting stream is a forward read of one such file, exposing one head id at
 * a time. Bounded memory (one open FILE + a small buffer per stream).
 */
#ifndef AIS_POST_H
#define AIS_POST_H

#include <stdio.h>

#include "ais.h"

/* Append ID to the posting list of (raw, unencoded) KEY under A's index,
 * creating idx/ and idx/<p>/ as needed. Returns 0, or -1 on error. */
int post_append(const ais *a, const char *key, long id);

/* Insert ID into KEY's posting list keeping the file ascending and duplicate
 * free. Fast path is an append when ID is larger than every existing id (the
 * common monotonic case); otherwise a bounded streaming merge into a temp file
 * that is renamed into place. Returns 0, or -1 on error. */
int post_insert(const ais *a, const char *key, long id);

/* A forward reader over one key's posting file. Bounded: an open FILE and the
 * current head. Declare on the stack; post_open then post_next/post_close. */
typedef struct post_stream {
    FILE *fp;       /* NULL once exhausted / never opened (empty stream) */
    long  head;     /* current id; valid only while alive == 1           */
    int   alive;    /* 1 if head holds a pending id, 0 if exhausted       */
} post_stream;

/* Open the posting stream for (raw) KEY and load its first head. A missing
 * posting file is a valid empty stream (alive == 0). Returns 0, or -1 on a
 * real error (path too long). */
int post_open(const ais *a, const char *key, post_stream *s);

/* Advance to the next id. Sets s->alive = 0 at end of file. Returns 0. */
int post_next(post_stream *s);

/* Close the stream's file. Safe to call repeatedly. */
void post_close(post_stream *s);

#endif /* AIS_POST_H */
