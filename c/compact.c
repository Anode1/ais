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
#include "win.h"          /* lstat/mkdir shims on native Windows; empty on POSIX */

/* Build "<dir>/<name>" into OUT. Returns 0, or -1 if it would not fit. */
static int compact_path(const ais *a, const char *name, char *out, size_t outsz)
{
    int n = snprintf(out, outsz, "%s/%s", a->dir, name);
    if (n < 0 || (size_t)n >= outsz)
        return -1;
    return 0;
}

/* Tombstone id with the delete-time TS and the record's content HASH (either may be
 * ""). Format: "id|ts|hash" (v2). Legacy "id"-only lines still read fine: tomb_contains
 * parses the leading id via atol, and a v1 entry's empty ts/hash sort as oldest and are
 * not exportable. The hash makes a deletion portable + compaction-proof for merge. */
int tomb_append(const ais *a, long id, const char *ts, const char *hash)
{
    char path[AIS_PATH_MAX];
    FILE *fp;

    if (compact_path(a, "tomb", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "a");
    if (fp == NULL)
        return -1;
    fprintf(fp, "%ld|%s|%s\n", id, ts ? ts : "", hash ? hash : "");
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

/* Stream each tomb entry (id, ts, hash) through CB in file order. ts/hash come back
 * "" for a legacy v1 entry. Returns 0, the callback's stop code, or -1 on error. */
int tomb_each(const ais *a, tomb_cb cb, void *ctx)
{
    char path[AIS_PATH_MAX], line[AIS_LINE_MAX];
    FILE *fp;
    int rc = 0;

    if (compact_path(a, "tomb", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "r");
    if (fp == NULL)
        return (errno == ENOENT) ? 0 : -1;
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *t, *h, *nl;
        long id = atol(line);
        nl = strchr(line, '\n');
        if (nl != NULL)
            *nl = '\0';
        t = strchr(line, '|');
        if (t != NULL) {
            *t++ = '\0';
            h = strchr(t, '|');
            if (h != NULL) *h++ = '\0';
            else h = (char *)"";
        } else {
            t = (char *)"";
            h = (char *)"";
        }
        rc = cb(id, t, h, ctx);
        if (rc != 0)
            break;
    }
    fclose(fp);
    return rc;
}

/* Parse one "id|key" ktomb line IN PLACE: returns the id (>= 0), sets *KP to the
 * key (text after the first '|', newline trimmed), or -1 if the line has no '|'. */
static long ktomb_parse(char *line, const char **kp)
{
    char *bar = strchr(line, '|');
    char *e;

    if (bar == NULL)
        return -1;
    *bar = '\0';
    *kp = bar + 1;
    e = bar + 1 + strlen(bar + 1);
    while (e > bar + 1 && (e[-1] == '\n' || e[-1] == '\r'))
        *--e = '\0';
    return atol(line);
}

int ktomb_append(const ais *a, long id, const char *key)
{
    char path[AIS_PATH_MAX];
    FILE *fp;

    if (compact_path(a, "ktomb", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "a");
    if (fp == NULL)
        return -1;
    fprintf(fp, "%ld|%s\n", id, key);
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

int ktomb_contains(const ais *a, long id, const char *key)
{
    char path[AIS_PATH_MAX];
    char line[AIS_LINE_MAX];
    FILE *fp;
    int found = 0;

    if (compact_path(a, "ktomb", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "r");
    if (fp == NULL)
        return (errno == ENOENT) ? 0 : -1;

    while (fgets(line, sizeof(line), fp) != NULL) {
        const char *k = NULL;
        if (ktomb_parse(line, &k) == id && k != NULL && strcmp(k, key) == 0) {
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found;
}

int ktomb_remove(const ais *a, long id, const char *key)
{
    char path[AIS_PATH_MAX], tmp[AIS_PATH_MAX];
    char orig[AIS_LINE_MAX], work[AIS_LINE_MAX];
    FILE *in, *out;
    long kept = 0;
    int removed = 0;

    if (compact_path(a, "ktomb", path, sizeof(path)) != 0)
        return -1;
    in = fopen(path, "r");
    if (in == NULL)
        return (errno == ENOENT) ? 0 : -1;   /* nothing to remove */

    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        fclose(in);
        return -1;
    }
    out = fopen(tmp, "w");
    if (out == NULL) {
        fclose(in);
        return -1;
    }
    while (fgets(orig, sizeof(orig), in) != NULL) {
        const char *k = NULL;
        snprintf(work, sizeof(work), "%s", orig);   /* parse a copy; emit orig */
        if (ktomb_parse(work, &k) == id && k != NULL && strcmp(k, key) == 0) {
            removed = 1;
            continue;                                /* drop this pair */
        }
        fputs(orig, out);
        kept++;
    }
    fclose(in);
    if (fclose(out) != 0) {
        unlink(tmp);
        return -1;
    }
    if (!removed) {                  /* pair not present: leave the file as it was */
        unlink(tmp);
        return 0;
    }
    if (kept == 0) {                 /* emptied: drop the file */
        unlink(tmp);
        unlink(path);
        return 0;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int ktomb_active(const ais *a)
{
    char path[AIS_PATH_MAX];
    struct stat st;

    if (compact_path(a, "ktomb", path, sizeof(path)) != 0)
        return -1;
    if (stat(path, &st) != 0)
        return (errno == ENOENT) ? 0 : -1;
    return st.st_size > 0;
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
    FILE *off_out;     /* rebuilding "off": first-line offset per id          */
    FILE *multi_out;   /* rebuilding "multi": ids that carry >1 line          */
    long  maxid;       /* highest id seen; first-appearance test + next_id    */
    long  last_off_id; /* highest id written to "off" (drives gap sentinels)  */
    int   filter;      /* 1 if ktomb has entries: strip detached keys         */
    int   error;
};

/* Write KEYS minus any key detached from ID (per ktomb) into OUT. Tokens are
 * space-separated, order preserved. Returns 0, or -1 on error / overflow. */
static int compact_visible_keys(const ais *a, long id, const char *keys,
                                char *out, size_t outsz)
{
    char buf[AIS_LINE_MAX];
    char *tok, *save;
    size_t used = 0;
    int n;

    n = snprintf(buf, sizeof(buf), "%s", keys);
    if (n < 0 || (size_t)n >= sizeof(buf))
        return -1;
    out[0] = '\0';
    for (tok = strtok_r(buf, " \t", &save); tok != NULL;
         tok = strtok_r(NULL, " \t", &save)) {
        int t = ktomb_contains(a, id, tok);
        if (t < 0)
            return -1;
        if (t == 1)
            continue;                /* detached: strip permanently */
        n = snprintf(out + used, outsz - used, "%s%s", used ? " " : "", tok);
        if (n < 0 || used + (size_t)n >= outsz)
            return -1;
        used += (size_t)n;
    }
    return 0;
}

/* Copy one surviving store line into store.new and re-post its keys. The line's
 * original timestamp is carried through unchanged (compaction preserves when a
 * record was saved); a legacy line with no ts stays legacy. */
static int compact_line(long id, const char *ts, const char *keys,
                        const char *value, void *vp)
{
    struct compact_ctx *c = vp;
    int t = tomb_contains(c->a, id);
    char keysbuf[AIS_LINE_MAX];
    char fkeys[AIS_LINE_MAX];
    const char *wkeys = keys;                /* keys to write + re-post */
    char *tok, *save;
    long offset;
    int n;

    if (t < 0) {
        c->error = 1;
        return -1;
    }
    if (t == 1)
        return 0;   /* dropped */

    if (c->filter) {                         /* strip keys detached from this id */
        if (compact_visible_keys(c->a, id, keys, fkeys, sizeof(fkeys)) != 0) {
            c->error = 1;
            return -1;
        }
        wkeys = fkeys;
    }

    offset = ftell(c->out);                  /* this line's offset in store.new */
    if (ts != NULL && ts[0] != '\0')
        fprintf(c->out, "%ld|%s|%s|%s\n", id, ts, wkeys, value);
    else
        fprintf(c->out, "%ld|%s|%s\n", id, wkeys, value);

    /* Re-index keys once per record, on its FIRST appearance. Records are first
     * written in id order, so a record's first line is the one whose id exceeds
     * every id seen so far; its add-continuation lines (appended later, hence
     * non-adjacent) repeat an already-seen id and are skipped. This both keeps
     * posting files ascending (ids appended in increasing order) and free of
     * duplicate ids. */
    if (id <= c->maxid) {
        fprintf(c->multi_out, "%ld\n", id);  /* a continuation: id is multi-line */
        return 0;
    }
    /* first appearance: sentinel-fill the gap left by any dropped ids, then
     * record this id's first-line offset so "off" stays indexed by id. */
    while (c->last_off_id + 1 < id) {
        off_write(c->off_out, -1);
        c->last_off_id++;
    }
    off_write(c->off_out, offset);
    c->last_off_id = id;
    c->maxid = id;

    n = snprintf(keysbuf, sizeof(keysbuf), "%s", wkeys);
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

static int compact_locked(ais *a)
{
    char storepath[AIS_PATH_MAX];
    char newpath[AIS_PATH_MAX];
    char idxpath[AIS_PATH_MAX];
    char tombpath[AIS_PATH_MAX];
    struct compact_ctx c;
    int rc = -1;

    c.a = a;
    c.out = NULL;
    c.off_out = NULL;
    c.multi_out = NULL;
    c.maxid = 0;
    c.last_off_id = 0;
    c.error = 0;
    c.filter = ktomb_active(a);              /* strip detached keys if any */
    if (c.filter < 0)
        return -1;

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

    {
        char offp[AIS_PATH_MAX], multip[AIS_PATH_MAX];
        if (compact_path(a, "off", offp, sizeof(offp)) != 0 ||
            compact_path(a, "multi", multip, sizeof(multip)) != 0)
            goto cleanup;
        c.off_out = fopen(offp, "w");        /* truncate + rebuild from scratch */
        c.multi_out = fopen(multip, "w");
        if (c.off_out == NULL || c.multi_out == NULL)
            goto cleanup;
    }

    if (store_each_record(a, compact_line, &c) != 0 || c.error)
        goto cleanup;

    if (fclose(c.out) != 0) {
        c.out = NULL;
        goto cleanup;
    }
    c.out = NULL;
    if (fclose(c.off_out) != 0) {
        c.off_out = NULL;
        goto cleanup;
    }
    c.off_out = NULL;
    if (fclose(c.multi_out) != 0) {
        c.multi_out = NULL;
        goto cleanup;
    }
    c.multi_out = NULL;

    if (rename(newpath, storepath) != 0)
        goto cleanup;

    /* clear the tombstone logs (truncate): detached keys are now stripped from
     * the rewritten store, and dropped records are gone, so both logs reset. */
    {
        char ktombpath[AIS_PATH_MAX];
        FILE *t = fopen(tombpath, "w");
        if (t != NULL)
            fclose(t);
        if (compact_path(a, "ktomb", ktombpath, sizeof(ktombpath)) == 0) {
            t = fopen(ktombpath, "w");
            if (t != NULL)
                fclose(t);
        }
    }

    a->next_id = c.maxid + 1;
    if (store_save_next_id(a) != 0)
        goto cleanup;

    store_write_version(a);   /* refresh the format stamp to the current version */
    rc = 0;

cleanup:
    if (c.out != NULL) {
        fclose(c.out);
        unlink(newpath);
    }
    if (c.off_out != NULL)
        fclose(c.off_out);
    if (c.multi_out != NULL)
        fclose(c.multi_out);
    return rc;
}

/* Compaction rewrites the whole store and rebuilds the index, so it holds the
 * exclusive writer lock for the entire operation. */
int ais_compact(ais *a)
{
    int rc;

    if (store_wlock(a) != 0)
        return -1;
    rc = compact_locked(a);
    store_wunlock(a);
    return rc;
}
