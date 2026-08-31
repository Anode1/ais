/* find.c -- content search. See find.h. Same shape as ais_dump: stream the
 * store one record at a time, drop tombstoned ids, emit only lines whose value
 * contains the needle, compared case-insensitively (ASCII). An empty needle
 * matches every value. Memory is bounded by the line buffer in store.c. */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "store.h"     /* store_each_record */
#include "compact.h"   /* tomb_contains    */
#include "find.h"

/* ASCII lower. Not tolower(): what folds above 0x7F is a locale question, and
 * a match must answer it the same on every machine (the key-encoding rule). */
static int find_lc(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* Case-insensitive substring (strcasestr semantics, ASCII). strcasestr itself
 * is nonstandard and needs a feature macro, so the loop is spelled out. An
 * empty NEEDLE matches, as strstr's does. */
static const char *find_ci(const char *hay, const char *needle)
{
    size_t i;

    for (;; hay++) {
        for (i = 0; needle[i] != '\0'; i++)
            if (find_lc((unsigned char)hay[i]) != find_lc((unsigned char)needle[i]))
                break;
        if (needle[i] == '\0')
            return hay;
        if (*hay == '\0')
            return NULL;
    }
}

struct find_ctx {
    ais        *a;
    const char *needle;
    FILE       *out;
    long        matched;   /* live lines printed: the CLI exits 1 on zero */
};

static int find_line(long id, const char *ts, const char *keys,
                     const char *value, void *vp)
{
    struct find_ctx *F = vp;
    int t;

    (void)ts;
    (void)keys;
    if (find_ci(value, F->needle) == NULL)
        return 0;                  /* this value does not match: skip */
    t = tomb_contains(F->a, id);
    if (t < 0)
        return -1;
    if (t == 0) {
        fprintf(F->out, "%ld|%s\n", id, value);
        F->matched++;
    }
    return 0;
}

long ais_find(ais *a, const char *needle, FILE *out)
{
    struct find_ctx F;
    F.a = a;
    F.needle = needle;
    F.out = out;
    F.matched = 0;
    if (store_each_record(a, find_line, &F) < 0)
        return -1;
    return F.matched;
}
