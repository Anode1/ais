/* compact.h -- tombstones and compaction.
 *
 * del(id) appends an id to INDEX/tomb; get/dump merge it out. Physical removal
 * happens only at compaction, which streams the store dropping tombstoned ids
 * into store.new, rebuilds idx/, renames atomically, clears tomb, and
 * recomputes next_id. Bounded buffers throughout.
 */
#ifndef AIS_COMPACT_H
#define AIS_COMPACT_H

#include "ais.h"

/* Append ID to INDEX/tomb. Returns 0, or -1 on error. */
int tomb_append(const ais *a, long id);

/* Is ID tombstoned? Streams INDEX/tomb. Returns 1 yes, 0 no, -1 on error.
 * Bounded memory; O(tomb) per call. */
int tomb_contains(const ais *a, long id);

/* Streaming compaction. Returns 0 on success, -1 on error. */
int ais_compact(ais *a);

#endif /* AIS_COMPACT_H */
