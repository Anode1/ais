/* find.h -- content search over record values.
 *
 * ais_find prints every LIVE record whose value contains NEEDLE (a plain
 * substring, case-sensitive) as "id|value", one line per matching value.
 * Streaming and bounded memory (store_each_record + tomb_contains, like dump);
 * tombstoned records are suppressed. Returns 0 on success, -1 on error.
 */
#ifndef AIS_FIND_H
#define AIS_FIND_H

#include <stdio.h>
#include "ais.h"

int ais_find(ais *a, const char *needle, FILE *out);

#endif /* AIS_FIND_H */
