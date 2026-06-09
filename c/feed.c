/* feed.c -- bulk feeding values into the index: `put -` (stdin) and
 * `put -R DIR` (directory walk). One aspect, "file many values under given
 * keys", kept out of main.c so the CLI dispatcher stays linear.
 *
 * Front-end code: it may die() on error, the same as main.c (the engine
 * modules only return codes). */
#define _XOPEN_SOURCE 700      /* nftw */
#include <stdio.h>
#include <string.h>
#include <ftw.h>

#include "feed.h"
#include "log.h"

/* feed_stdin: file each non-empty stdin line, verbatim, as a value under KEYS. */
void feed_stdin(ais *a, const char *keys)
{
    char line[AIS_LINE_MAX];

    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n == 0)
            continue;                  /* skip blank lines */
        if (ais_put(a, keys, line) < 0)
            die("put -: failed on '%s'", line);
    }
}

/* nftw() has no user-context parameter, so the walk state is file-static. */
static ais        *g_ais;
static const char *g_keys;
static int         g_failed;

static int put_one_file(const char *path, const struct stat *st,
                         int typeflag, struct FTW *ftw)
{
    (void)st;
    (void)ftw;
    if (typeflag != FTW_F)
        return 0;                      /* directories, symlinks, etc: skip */
    if (ais_put(g_ais, g_keys, path) < 0) {
        g_failed = 1;
        return 1;                      /* stop the walk */
    }
    return 0;
}

/* feed_dir: file every regular file under DIR (recursive) as a value under KEYS. */
void feed_dir(ais *a, const char *dir, const char *keys)
{
    g_ais = a;
    g_keys = keys;
    g_failed = 0;
    if (nftw(dir, put_one_file, 16, FTW_PHYS) != 0 && g_failed)
        die("put -R: indexing '%s' failed", dir);
}
