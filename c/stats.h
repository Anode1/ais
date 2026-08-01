/* stats.h -- index statistics: live records, keys, and tombstones. */
#ifndef AIS_STATS_H
#define AIS_STATS_H

#include <stdio.h>
#include "ais.h"

/* Print three lines to OUT describing the INDEX:
 *     records: <N>    distinct live record ids (store ids not in tomb)
 *     keys: <M>       key files under idx/<p>/
 *     deleted: <K>    distinct tombstoned ids
 * Streams every file with a fixed line buffer; memory never tracks data size.
 * A missing file or directory counts as 0. Returns 0 on success, -1 on error. */
int ais_stats(ais *a, FILE *out);

/* Just the first of those numbers: distinct LIVE record ids. Separated out
 * because a front-end wants the figure, not a formatted report -- after a sync,
 * "your library now holds N records" is the only confirmation the user gets that
 * anything actually arrived, and printing to a FILE to scrape it back is not a
 * seam an FFI caller can use. Same streaming count, same fixed buffers.
 * Returns 0 and sets *OUT, or -1 on error. */
int ais_count_live(const ais *a, long *out);

#endif /* AIS_STATS_H */
