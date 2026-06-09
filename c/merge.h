/* merge.h -- k-way streaming merge over sorted posting streams.
 *
 * Generalises the sorted two-pointer set ops (attic/set.c) from in-memory
 * arrays to file heads: each surviving id is emitted once, ascending, and
 * tombstoned ids are suppressed. Memory is O(nkeys) -- one head per key.
 *
 * AIS_AND = intersection (an id all streams hold), AIS_OR = union (any stream).
 */
#ifndef AIS_MERGE_H
#define AIS_MERGE_H

#include "ais.h"
#include "post.h"

/* Run the merge over the NSTREAMS open posting streams in MODE, emitting each
 * surviving (non-tombstoned) id through CB. Suppression is delegated to A
 * (tomb_contains). Returns 0, the callback's stop code, or -1 on error.
 * The streams are consumed (advanced to exhaustion) but not closed. */
int merge_run(ais *a, post_stream *streams, int nstreams, ais_mode mode,
              ais_id_cb cb, void *ctx);

#endif /* AIS_MERGE_H */
