/* find.h -- content search over record values.
 *
 * ais_find prints every live record whose value contains NEEDLE (a plain
 * substring, case-sensitive) as "id|value", one line per matching value.
 * Tombstoned records are suppressed. Streaming, bounded memory. Returns 0, or
 * -1 on error.
 */
#ifndef AIS_FIND_H
#define AIS_FIND_H

#include <stdio.h>
#include "ais.h"

int ais_find(ais *a, const char *needle, FILE *out);

#endif /* AIS_FIND_H */
