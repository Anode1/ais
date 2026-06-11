/* find.c -- content search. See find.h.
 *
 * Same shape as ais_dump: stream the store one record at a time, drop
 * tombstoned ids, but emit only lines whose value contains the needle.
 * Matching is a plain strstr (substring, case-sensitive); an empty needle
 * matches every value. Memory is bounded by the line buffer in store.c.
 */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "store.h"     /* store_each_record */
#include "compact.h"   /* tomb_contains    */
#include "find.h"

struct find_ctx {
    ais        *a;
    const char *needle;
    FILE       *out;
};

static int find_line(long id, const char *ts, const char *keys,
                     const char *value, void *vp)
{
    struct find_ctx *F = vp;
    int t;

    (void)ts;
    (void)keys;
    if (strstr(value, F->needle) == NULL)
        return 0;                  /* this value does not match: skip */
    t = tomb_contains(F->a, id);
    if (t < 0)
        return -1;
    if (t == 0)
        fprintf(F->out, "%ld|%s\n", id, value);
    return 0;
}

int ais_find(ais *a, const char *needle, FILE *out)
{
    struct find_ctx F;
    F.a = a;
    F.needle = needle;
    F.out = out;
    return store_each_record(a, find_line, &F);
}
