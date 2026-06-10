/* feed.c -- bulk feeding values into the index: `put -` (stdin) and
 * `put -R DIR` (directory walk). One aspect, "file many values under given
 * keys", kept out of main.c so the CLI dispatcher stays linear.
 *
 * Front-end code: it may die() on error, the same as main.c (the engine
 * modules only return codes). */
#define _XOPEN_SOURCE 700      /* nftw */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

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
static char        g_root[PATH_MAX];   /* worktree root, when the index is .ais/ */
static int         g_have_root;

/* The worktree root: if the index dir's basename is ".ais", files under its
 * PARENT are stored relative to that parent (git-style), so moving the whole
 * tree keeps the refs valid. Returns 1 and fills ROOT, or 0 when there is no
 * worktree root (a global or -f index -> absolute paths). */
static int feed_root(const ais *a, char *root, size_t rootsz)
{
    char absidx[PATH_MAX];
    char *slash;
    size_t n;

    if (realpath(a->dir, absidx) == NULL)
        return 0;
    slash = strrchr(absidx, '/');
    if (slash == NULL || strcmp(slash + 1, ".ais") != 0)
        return 0;                      /* not a .ais worktree index */
    n = (size_t)(slash - absidx);      /* parent path, minus "/.ais" */
    if (n == 0) {                      /* ".ais" sits at the filesystem root */
        root[0] = '/';
        root[1] = '\0';
        return 1;
    }
    if (n >= rootsz)
        return 0;
    memcpy(root, absidx, n);
    root[n] = '\0';
    return 1;
}

/* The value to store for PATH: canonical-absolute, but relative to ROOT when
 * ROOT is set and PATH lies under it. Falls back to PATH as given if it cannot
 * be resolved (e.g. it vanished mid-walk). */
static void feed_value(const char *path, char *out, size_t outsz)
{
    char abs[PATH_MAX];
    size_t rl;

    if (realpath(path, abs) == NULL) {
        snprintf(out, outsz, "%s", path);
        return;
    }
    if (g_have_root) {
        if (strcmp(g_root, "/") == 0) {
            snprintf(out, outsz, "%s", abs + 1);          /* drop leading '/' */
            return;
        }
        rl = strlen(g_root);
        if (strncmp(abs, g_root, rl) == 0 && abs[rl] == '/') {
            snprintf(out, outsz, "%s", abs + rl + 1);     /* relative to root */
            return;
        }
    }
    snprintf(out, outsz, "%s", abs);                      /* absolute */
}

static int put_one_file(const char *path, const struct stat *st,
                         int typeflag, struct FTW *ftw)
{
    char value[PATH_MAX];

    (void)st;
    (void)ftw;
    if (typeflag != FTW_F)
        return 0;                      /* directories, symlinks, etc: skip */
    feed_value(path, value, sizeof(value));
    if (ais_put(g_ais, g_keys, value) < 0) {
        g_failed = 1;
        return 1;                      /* stop the walk */
    }
    return 0;
}

/* feed_dir: file every regular file under DIR (recursive) as a value under KEYS.
 * Paths are stored relative to the .ais worktree root when there is one (so the
 * tree can move), else absolute. */
void feed_dir(ais *a, const char *dir, const char *keys)
{
    g_ais = a;
    g_keys = keys;
    g_failed = 0;
    g_have_root = feed_root(a, g_root, sizeof(g_root));
    if (nftw(dir, put_one_file, 16, FTW_PHYS) != 0 && g_failed)
        die("put -R: indexing '%s' failed", dir);
}

/* Strip a trailing newline/CR in place. */
static void chomp(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

void feed_interactive(ais *a, const char *base)
{
    const char *ttypath = getenv("AIS_TTY");   /* a file overrides the terminal */
    FILE *tty;
    char value[AIS_LINE_MAX];
    char typed[AIS_LINE_MAX];
    char keys[AIS_LINE_MAX];

    tty = fopen(ttypath != NULL ? ttypath : "/dev/tty", "r");
    if (tty == NULL)
        die("put -i: no terminal for keys (pipe values in, or set AIS_TTY=FILE)");

    /* Each stdin line is a value; ask the terminal for its keys (Enter accepts
     * the base keys). Values flow from the pipe, keys from the tty -- two
     * separate streams, which is the whole point of -i. */
    while (fgets(value, sizeof(value), stdin) != NULL) {
        chomp(value);
        if (value[0] == '\0')
            continue;                          /* skip blank input lines */

        if (base[0] != '\0')
            fprintf(stderr, "%s\n  keys (added to: %s) > ", value, base);
        else
            fprintf(stderr, "%s\n  keys > ", value);
        fflush(stderr);

        if (fgets(typed, sizeof(typed), tty) == NULL)
            break;                             /* EOF on the terminal -> done */
        chomp(typed);

        /* keys = base, then " " + typed if any. Two writes (not one "%s %s")
         * so the compiler can't flag a possible truncation. */
        {
            size_t kl = 0;
            keys[0] = '\0';
            if (base[0] != '\0') {
                int n = snprintf(keys, sizeof(keys), "%s", base);
                kl = (n > 0) ? (size_t)n : 0;
            }
            if (typed[0] != '\0' && kl < sizeof(keys))
                snprintf(keys + kl, sizeof(keys) - kl, "%s%s",
                         kl > 0 ? " " : "", typed);
        }

        if (keys[0] == '\0') {
            fprintf(stderr, "  (no keys given; skipped)\n");
            continue;
        }
        if (ais_put(a, keys, value) < 0)
            die("put -i: failed on '%s'", value);
    }
    fclose(tty);
}

void feed_import(ais *a)
{
    char line[AIS_LINE_MAX];
    long n = 0;

    /* Each line is "keys|value" (the inverse of dump's id|keys|value). The
     * store already forbids '|' in a value, so the first '|' is the split.
     * Blank lines and #-comments are skipped, so the file stays hand-editable. */
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *bar, *keys, *val, *e;

        chomp(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;
        bar = strchr(line, '|');
        if (bar == NULL) {
            fprintf(stderr, "import: no '|', skipped: %s\n", line);
            continue;
        }
        *bar = '\0';

        keys = line;
        while (*keys == ' ' || *keys == '\t')
            keys++;                            /* trim around the separator so */
        for (e = bar - 1; e >= keys && (*e == ' ' || *e == '\t'); e--)
            *e = '\0';                         /* "keys | value" works too     */
        val = bar + 1;
        while (*val == ' ' || *val == '\t')
            val++;

        if (*keys == '\0') {
            fprintf(stderr, "import: empty keys, skipped: %s\n", val);
            continue;
        }
        if (ais_put(a, keys, val) < 0)
            die("import: failed on '%s'", val);
        n++;
    }
    fprintf(stderr, "imported %ld\n", n);
}

void feed_doc(ais *a, const char *keys)
{
    char dirpath[AIS_PATH_MAX], blobpath[AIS_PATH_MAX], relval[AIS_PATH_MAX];
    char ts[32];
    time_t now;
    struct tm *lt;
    FILE *bf;
    char buf[8192];
    size_t n;
    long got;
    int seq;

    if (snprintf(dirpath, sizeof(dirpath), "%s/blobs", a->dir) >= (int)sizeof(dirpath))
        die("doc: path too long");
    if (mkdir(dirpath, 0777) != 0 && errno != EEXIST)
        die("doc: cannot create '%s'", dirpath);

    /* Name the blob by local timestamp: blobs/ then sorts chronologically and
     * is readable -- `ls blobs/` lists your documents in time order. A second
     * document within the same second gets a -N suffix, so names stay unique
     * with no hashing. */
    now = time(NULL);
    lt = localtime(&now);
    if (lt == NULL || strftime(ts, sizeof(ts), "%Y-%m-%d-%H%M%S", lt) == 0)
        die("doc: cannot format timestamp");
    for (seq = 1; seq < 10000; seq++) {
        if (seq == 1)
            snprintf(relval, sizeof(relval), "blobs/%s.txt", ts);
        else
            snprintf(relval, sizeof(relval), "blobs/%s-%d.txt", ts, seq);
        snprintf(blobpath, sizeof(blobpath), "%s/%s", a->dir, relval);
        if (access(blobpath, F_OK) != 0)
            break;                         /* a free name */
    }

    /* stream stdin into the blob file -- bounded memory, any size */
    bf = fopen(blobpath, "w");
    if (bf == NULL)
        die("doc: cannot write '%s'", blobpath);
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0)
        if (fwrite(buf, 1, n, bf) != n) {
            fclose(bf);
            die("doc: write failed");
        }
    if (fclose(bf) != 0)
        die("doc: close failed");

    /* store the relative path as the value */
    got = ais_put(a, keys, relval);
    if (got < 0)
        die("doc: put failed");
    printf("%ld|%s\n", got, relval);
}
