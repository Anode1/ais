/* compact.c -- tombstones and streaming compaction. See compact.h. */
#define _DEFAULT_SOURCE      /* mkdir, lstat, strtok_r, dirent */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "compact.h"
#include "post.h"
#include "store.h"

/* Build "<dir>/<name>" into OUT. Returns 0, or -1 if it would not fit. */
static int compact_path(const ais *a, const char *name, char *out, size_t outsz)
{
    int n = snprintf(out, outsz, "%s/%s", a->dir, name);
    if (n < 0 || (size_t)n >= outsz)
        return -1;
    return 0;
}

int tomb_append(const ais *a, long id)
{
    char path[AIS_PATH_MAX];
    FILE *fp;

    if (compact_path(a, "tomb", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "a");
    if (fp == NULL)
        return -1;
    fprintf(fp, "%ld\n", id);
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

int tomb_contains(const ais *a, long id)
{
    char path[AIS_PATH_MAX];
    char line[64];
    FILE *fp;
    int found = 0;

    if (compact_path(a, "tomb", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "r");
    if (fp == NULL)
        return (errno == ENOENT) ? 0 : -1;

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (atol(line) == id) {
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found;
}

/* Recursively remove a directory tree. Returns 0, or -1 on error. Bounded by
 * the index's directory depth (idx/<p>/<key>), not by data size. */
static int compact_rmtree(const char *path)
{
    DIR *d;
    struct dirent *e;
    struct stat st;
    char child[AIS_PATH_MAX];
    int rc = 0;

    if (lstat(path, &st) != 0)
        return (errno == ENOENT) ? 0 : -1;
    if (!S_ISDIR(st.st_mode))
        return (unlink(path) == 0) ? 0 : -1;

    d = opendir(path);
    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL) {
        int n;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        n = snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        if (n < 0 || (size_t)n >= sizeof(child)) {
            rc = -1;
            break;
        }
        if (compact_rmtree(child) != 0) {
            rc = -1;
            break;
        }
    }
    closedir(d);
    if (rc == 0 && rmdir(path) != 0)
        rc = -1;
    return rc;
}

/* Streaming pass state: the open store.new and the handle (for posting writes
 * and tomb lookups). */
struct compact_ctx {
    ais  *a;
    FILE *out;
    long  maxid;   /* highest id seen; first-appearance test + next_id source */
    int   error;
};

/* Copy one surviving store line into store.new and re-post its keys. */
static int compact_line(long id, const char *keys, const char *value, void *vp)
{
    struct compact_ctx *c = vp;
    int t = tomb_contains(c->a, id);
    char keysbuf[AIS_LINE_MAX];
    char *tok, *save;
    int n;

    if (t < 0) {
        c->error = 1;
        return -1;
    }
    if (t == 1)
        return 0;   /* dropped */

    fprintf(c->out, "%ld|%s|%s\n", id, keys, value);

    /* Re-index keys once per record, on its FIRST appearance. Records are first
     * written in id order, so a record's first line is the one whose id exceeds
     * every id seen so far; its add-continuation lines (appended later, hence
     * non-adjacent) repeat an already-seen id and are skipped. This both keeps
     * posting files ascending (ids appended in increasing order) and free of
     * duplicate ids. */
    if (id <= c->maxid)
        return 0;
    c->maxid = id;

    n = snprintf(keysbuf, sizeof(keysbuf), "%s", keys);
    if (n < 0 || (size_t)n >= sizeof(keysbuf)) {
        c->error = 1;
        return -1;
    }
    for (tok = strtok_r(keysbuf, " \t", &save); tok != NULL;
         tok = strtok_r(NULL, " \t", &save)) {
        if (post_append(c->a, tok, id) != 0) {
            c->error = 1;
            return -1;
        }
    }
    return 0;
}

int ais_compact(ais *a)
{
    char storepath[AIS_PATH_MAX];
    char newpath[AIS_PATH_MAX];
    char idxpath[AIS_PATH_MAX];
    char tombpath[AIS_PATH_MAX];
    struct compact_ctx c;
    int rc = -1;

    c.a = a;
    c.out = NULL;
    c.maxid = 0;
    c.error = 0;

    if (compact_path(a, "store", storepath, sizeof(storepath)) != 0 ||
        compact_path(a, "store.new", newpath, sizeof(newpath)) != 0 ||
        compact_path(a, "idx", idxpath, sizeof(idxpath)) != 0 ||
        compact_path(a, "tomb", tombpath, sizeof(tombpath)) != 0)
        return -1;

    /* drop the old index; compact_line rebuilds it as it streams */
    if (compact_rmtree(idxpath) != 0)
        return -1;

    c.out = fopen(newpath, "w");
    if (c.out == NULL)
        return -1;

    if (store_each_record(a, compact_line, &c) != 0 || c.error)
        goto cleanup;

    if (fclose(c.out) != 0) {
        c.out = NULL;
        goto cleanup;
    }
    c.out = NULL;

    if (rename(newpath, storepath) != 0)
        goto cleanup;

    /* clear the tombstone log (truncate) */
    {
        FILE *t = fopen(tombpath, "w");
        if (t != NULL)
            fclose(t);
    }

    a->next_id = c.maxid + 1;
    if (store_save_next_id(a) != 0)
        goto cleanup;

    rc = 0;

cleanup:
    if (c.out != NULL) {
        fclose(c.out);
        unlink(newpath);
    }
    return rc;
}
