/* ais.c -- the public facade. Composes store/post/merge/compact into the
 * ais.h API. Modules return 0/-1; this layer does the same (only main.c dies).
 */
#define _DEFAULT_SOURCE      /* strtok_r */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ais.h"
#include "compact.h"
#include "log.h"
#include "merge.h"
#include "post.h"
#include "store.h"

int ais_open(ais *a, const char *dir)
{
    if (store_open(a, dir) != 0) {
        debug("cannot open index '%s' (in use, or unwritable)", dir);
        return -1;
    }
    debug("opened index '%s', next_id=%ld", a->dir, a->next_id);
    return 0;
}

void ais_close(ais *a)
{
    store_close(a);
}

/* Post each whitespace-separated key of KEYS to record ID. post_insert keeps
 * each posting file ascending and duplicate-free, so re-puts add nothing and a
 * reused (old) id lands in order even in a key file that already has larger
 * ids. Returns 0/-1. */
static int ais_post_keys(ais *a, const char *keys, long id)
{
    char buf[AIS_LINE_MAX];
    char *tok, *save;
    int n;

    n = snprintf(buf, sizeof(buf), "%s", keys);
    if (n < 0 || (size_t)n >= sizeof(buf))
        return -1;
    for (tok = strtok_r(buf, " \t", &save); tok != NULL;
         tok = strtok_r(NULL, " \t", &save))
        if (post_insert(a, tok, id) != 0)
            return -1;
    return 0;
}

long ais_put(ais *a, const char *keys, const char *value)
{
    long id = 0, rc;
    int found;

    if (store_wlock(a) != 0)                 /* one writer at a time */
        return -1;
    if (store_load_next_id(a) != 0) {        /* fresh id under the lock */
        store_wunlock(a);
        return -1;
    }

    found = store_find_value(a, value, &id);
    if (found < 0) { rc = -1; goto out; }

    if (found) {
        /* value already stored: reuse its id, just (re)post the keys (the merge
         * dedups, so re-posting an existing id is harmless). */
        debug("put: value exists as id=%ld (idempotent)", id);
        rc = (ais_post_keys(a, keys, id) != 0) ? -1 : id;
        goto out;
    }

    id = a->next_id;
    {
        char ts[AIS_TS_MAX];
        long off = store_bytes(a);          /* offset the new line gets */
        int  ok  = (off >= 0) && off_consistent(a);
        store_now(ts, sizeof(ts));          /* "" if the clock is unreadable */
        if (store_append(a, id, ts, keys, value) != 0) { rc = -1; goto out; }
        if (ok && off_append(a, off) != 0)             { rc = -1; goto out; }  /* keep "off" in lockstep */
    }
    if (ais_post_keys(a, keys, id) != 0) { rc = -1; goto out; }
    a->next_id = id + 1;
    if (store_save_next_id(a) != 0) { rc = -1; goto out; }
    debug("put: new id=%ld", id);
    rc = id;

out:
    store_wunlock(a);
    return rc;
}

/* Context for ais_add: find whether the id exists and capture its keys. */
struct add_lookup {
    long  id;
    int   found;
    char  keys[AIS_LINE_MAX];
};

static int add_seek(long id, const char *ts, const char *keys,
                    const char *value, void *vp)
{
    struct add_lookup *L = vp;
    (void)ts;
    (void)value;
    if (id == L->id) {
        L->found = 1;
        snprintf(L->keys, sizeof(L->keys), "%s", keys);
        return -1;   /* stop: first occurrence carries the keys */
    }
    return 0;
}

int ais_add(ais *a, long id, const char *value)
{
    struct add_lookup L;
    int scan, rc = -1;

    if (store_wlock(a) != 0)
        return -1;

    L.id = id;
    L.found = 0;
    L.keys[0] = '\0';

    scan = store_each_record(a, add_seek, &L);
    if (scan < -1)          /* a real error (callback stops with -1) */
        goto out;
    if (!L.found)
        goto out;

    /* continuation line: same id, same keys field, the new value, stamped with
     * its own (later) save time. Keys are already posted to this id, so no
     * re-post. */
    {
        char ts[AIS_TS_MAX];
        store_now(ts, sizeof(ts));
        if (store_append(a, id, ts, L.keys, value) != 0)
            goto out;
    }
    if (multi_append(a, id) != 0)           /* id now has >1 line */
        goto out;
    debug("add: appended link to id=%ld", id);
    rc = 0;
out:
    store_wunlock(a);
    return rc;
}

int ais_del(ais *a, long id)
{
    int rc;

    if (store_wlock(a) != 0)
        return -1;
    rc = tomb_append(a, id);
    store_wunlock(a);
    if (rc == 0)
        debug("del: tombstoned id=%ld", id);
    return rc;
}

int ais_del_key(ais *a, const char *key)
{
    post_stream s;
    int n = 0, rc;

    if (store_wlock(a) != 0)
        return -1;
    if (post_open(a, key, &s) != 0) {
        store_wunlock(a);
        return -1;
    }

    for (; s.alive; post_next(&s)) {
        if (tomb_append(a, s.head) != 0) {
            rc = -1;
            goto cleanup;
        }
        n++;
    }
    rc = n;

cleanup:
    post_close(&s);
    store_wunlock(a);
    debug("del_key: '%s' tombstoned %d records", key, n);
    return rc;
}

int ais_get(ais *a, char *const keys[], int nkeys, ais_mode mode,
            ais_id_cb cb, void *ctx)
{
    post_stream streams[AIS_KEYS_MAX];
    int i, opened = 0, rc;

    if (nkeys <= 0)
        return 0;
    if (nkeys > AIS_KEYS_MAX)
        nkeys = AIS_KEYS_MAX;

    for (i = 0; i < nkeys; i++) {
        if (post_open(a, keys[i], &streams[i]) != 0) {
            rc = -1;
            goto cleanup;
        }
        opened++;
    }

    rc = merge_run(a, streams, nkeys, mode, cb, ctx);

cleanup:
    for (i = 0; i < opened; i++)
        post_close(&streams[i]);
    return rc;
}

/* Context for ais_record: forward callback only for the matching id. */
struct record_ctx {
    long        id;
    ais_val_cb  cb;
    void       *ctx;
    int         stop;
};

static int record_seek(long id, const char *ts, const char *keys,
                       const char *value, void *vp)
{
    struct record_ctx *R = vp;
    (void)ts;
    (void)keys;
    if (id == R->id) {
        R->stop = R->cb(id, value, R->ctx);
        return R->stop;   /* nonzero -> stop the scan */
    }
    return 0;
}

int ais_record(ais *a, long id, ais_val_cb cb, void *ctx)
{
    struct record_ctx R;
    long offset;

    /* Fast path: a single-line record whose first-line offset is in "off".
     * Multi-line records (ais_add) and any miss fall through to the scan, and
     * store_value_at re-checks the line's id, so this can never be wrong. */
    if (multi_contains(a, id) == 0 && off_get(a, id, &offset) == 1 &&
        store_value_at(a, id, offset, cb, ctx) == 1)
        return 0;

    R.id = id;
    R.cb = cb;
    R.ctx = ctx;
    R.stop = 0;
    store_each_record(a, record_seek, &R);
    return 0;
}

/* Buffer of distinct key names gathered from idx/, to be sorted then emitted.
 * Bounded: AIS_KEYS_LIST_MAX names of AIS_KEY_MAX each (a listing command, not
 * a streaming query -- keys are human words, not astronomically many). */
#define AIS_KEYS_LIST_MAX 65536

struct key_buf {
    char (*names)[AIS_KEY_MAX];   /* one heap block, freed on every path */
    int    n;
    int    cap;
};

static int key_buf_add(struct key_buf *b, const char *name)
{
    if (b->n >= b->cap)
        return -1;   /* listing exceeds the bounded buffer */
    snprintf(b->names[b->n], AIS_KEY_MAX, "%s", name);
    b->n++;
    return 0;
}

static int key_name_cmp(const void *pa, const void *pb)
{
    return strcmp((const char *)pa, (const char *)pb);
}

int ais_keys(ais *a, ais_key_cb cb, void *ctx)
{
    char idxdir[AIS_PATH_MAX];
    struct key_buf b;
    DIR *idx = NULL;
    struct dirent *pe;
    int i, rc = 0;

    b.names = NULL;
    b.n = 0;
    b.cap = AIS_KEYS_LIST_MAX;

    if (snprintf(idxdir, sizeof(idxdir), "%s/idx", a->dir) >= (int)sizeof(idxdir))
        return -1;
    idx = opendir(idxdir);
    if (idx == NULL)
        return 0;   /* no idx/ yet: no keys */

    /* one bounded heap block holding the collected key names */
    b.names = malloc((size_t)b.cap * sizeof(b.names[0]));
    if (b.names == NULL) {
        rc = -1;
        goto cleanup;
    }

    /* walk each prefix dir (names of length 1 or 2), collecting its key files */
    while ((pe = readdir(idx)) != NULL) {
        char pdir[AIS_PATH_MAX];
        DIR *pd;
        struct dirent *ke;

        if (pe->d_name[0] == '.')   /* skip "." and ".." */
            continue;
        if (snprintf(pdir, sizeof(pdir), "%s/%s", idxdir, pe->d_name)
                >= (int)sizeof(pdir))
            continue;
        pd = opendir(pdir);
        if (pd == NULL)
            continue;   /* not a dir (or unreadable): skip, stay local */
        while ((ke = readdir(pd)) != NULL) {
            if (ke->d_name[0] == '.')
                continue;
            if (key_buf_add(&b, ke->d_name) != 0) {
                rc = -1;
                closedir(pd);
                goto cleanup;
            }
        }
        closedir(pd);
    }

    /* sort so output is stable and navigable; emit distinct (skip equal runs) */
    qsort(b.names, (size_t)b.n, sizeof(b.names[0]), key_name_cmp);
    for (i = 0; i < b.n; i++) {
        if (i > 0 && strcmp(b.names[i], b.names[i - 1]) == 0)
            continue;
        rc = cb(b.names[i], ctx);
        if (rc != 0)
            goto cleanup;
    }

cleanup:
    free(b.names);
    if (idx != NULL)
        closedir(idx);
    return rc;
}

/* Context for ais_dump: print live store lines (tombstones merged out). */
struct dump_ctx {
    ais  *a;
    FILE *out;
};

static int dump_line(long id, const char *ts, const char *keys,
                     const char *value, void *vp)
{
    struct dump_ctx *D = vp;
    int t = tomb_contains(D->a, id);
    (void)ts;   /* dump is the content serialization; id and ts are reassigned
                 * on re-import, so dump stays id|keys|value (see feed_import) */
    if (t < 0)
        return -1;
    if (t == 0)
        fprintf(D->out, "%ld|%s|%s\n", id, keys, value);
    return 0;
}

void ais_dump(ais *a, FILE *out)
{
    struct dump_ctx D;
    D.a = a;
    D.out = out;
    store_each_record(a, dump_line, &D);
}

/* --- timeline: the most-recent live records, dateless ones surfaced first --- */
#define AIS_TL_DEFAULT  500    /* records kept when LIMIT <= 0                  */
#define AIS_TL_MAX    10000    /* hard cap on the ring (bounds the heap block)  */
#define AIS_TL_VAL_MAX 2048    /* value snippet held per row (full value: recall)*/

struct tl_entry {
    long id;
    char ts[AIS_TS_MAX];
    char keys[AIS_KEY_MAX];
    char value[AIS_TL_VAL_MAX];
};

struct tl_ctx {
    ais             *a;
    struct tl_entry *ring;
    int              cap;     /* ring size; keeps the last `cap` live lines     */
    long             count;   /* live lines seen so far                         */
};

static int tl_collect(long id, const char *ts, const char *keys,
                      const char *value, void *vp)
{
    struct tl_ctx *c = vp;
    struct tl_entry *e;
    int t = tomb_contains(c->a, id);

    if (t < 0)
        return -1;
    if (t == 1)
        return 0;             /* deleted: not in the timeline */

    e = &c->ring[c->count % c->cap];   /* overwrite oldest once full */
    e->id = id;
    snprintf(e->ts,    sizeof(e->ts),    "%s", ts);
    snprintf(e->keys,  sizeof(e->keys),  "%s", keys);
    snprintf(e->value, sizeof(e->value), "%s", value);
    c->count++;
    return 0;
}

/* Order for the view: dateless rows FIRST (an empty ts sorts before any), then
 * newest date first (ISO text sorts chronologically), ties by higher id. */
static int tl_cmp(const void *pa, const void *pb)
{
    const struct tl_entry *a = pa, *b = pb;
    int au = (a->ts[0] == '\0'), bu = (b->ts[0] == '\0');
    int c;

    if (au != bu)
        return au ? -1 : 1;
    if (!au) {
        c = strcmp(b->ts, a->ts);     /* descending: newest first */
        if (c != 0)
            return c;
    }
    return (a->id < b->id) ? 1 : (a->id > b->id) ? -1 : 0;
}

int ais_timeline(ais *a, int limit, ais_tl_cb cb, void *ctx)
{
    struct tl_ctx c;
    int n, i, rc = 0;

    if (limit <= 0)
        limit = AIS_TL_DEFAULT;
    if (limit > AIS_TL_MAX)
        limit = AIS_TL_MAX;

    c.a = a;
    c.cap = limit;
    c.count = 0;
    c.ring = malloc((size_t)limit * sizeof(c.ring[0]));
    if (c.ring == NULL)
        return -1;

    if (store_each_record(a, tl_collect, &c) < 0) {
        free(c.ring);
        return -1;
    }

    /* The kept entries are exactly ring[0..n-1]: with fewer than `cap` lines
     * they fill 0..count-1; once full, every slot holds one of the last `cap`. */
    n = (c.count < (long)c.cap) ? (int)c.count : c.cap;
    qsort(c.ring, (size_t)n, sizeof(c.ring[0]), tl_cmp);
    for (i = 0; i < n; i++) {
        rc = cb(c.ring[i].id, c.ring[i].ts, c.ring[i].keys, c.ring[i].value, ctx);
        if (rc != 0)
            break;
    }
    free(c.ring);
    return rc;
}

/* --- tags: every distinct key with its posting count, busiest first --------- */
struct tag_entry {
    char key[AIS_KEY_MAX];
    long count;
};

/* Count the lines (postings) in one key file idx/<p>/<key>. */
static long tag_count_file(const char *path)
{
    char buf[8192];
    FILE *fp;
    long n = 0;
    size_t r;

    fp = fopen(path, "r");
    if (fp == NULL)
        return 0;
    while ((r = fread(buf, 1, sizeof(buf), fp)) > 0) {
        size_t i;
        for (i = 0; i < r; i++)
            if (buf[i] == '\n')
                n++;
    }
    fclose(fp);
    return n;
}

static int tag_cmp(const void *pa, const void *pb)
{
    const struct tag_entry *a = pa, *b = pb;
    if (a->count != b->count)
        return (a->count < b->count) ? 1 : -1;   /* count descending */
    return strcmp(a->key, b->key);               /* ties: key ascending */
}

int ais_tags(ais *a, ais_tag_cb cb, void *ctx)
{
    char idxdir[AIS_PATH_MAX];
    struct tag_entry *tags = NULL;
    int ntags = 0, cap = AIS_KEYS_LIST_MAX, i, rc = 0;
    DIR *idx;
    struct dirent *pe;

    if (snprintf(idxdir, sizeof(idxdir), "%s/idx", a->dir) >= (int)sizeof(idxdir))
        return -1;
    idx = opendir(idxdir);
    if (idx == NULL)
        return 0;             /* no idx/ yet: no tags */

    tags = malloc((size_t)cap * sizeof(tags[0]));
    if (tags == NULL) {
        closedir(idx);
        return -1;
    }

    /* walk each prefix dir, counting postings per key file */
    while ((pe = readdir(idx)) != NULL) {
        char pdir[AIS_PATH_MAX];
        DIR *pd;
        struct dirent *ke;

        if (pe->d_name[0] == '.')
            continue;
        if (snprintf(pdir, sizeof(pdir), "%s/%s", idxdir, pe->d_name)
                >= (int)sizeof(pdir))
            continue;
        pd = opendir(pdir);
        if (pd == NULL)
            continue;
        while ((ke = readdir(pd)) != NULL) {
            char kpath[AIS_PATH_MAX];
            if (ke->d_name[0] == '.')
                continue;
            if (ntags >= cap) { rc = -1; closedir(pd); goto cleanup; }
            if (snprintf(kpath, sizeof(kpath), "%s/%s", pdir, ke->d_name)
                    >= (int)sizeof(kpath))
                continue;
            snprintf(tags[ntags].key, sizeof(tags[ntags].key), "%s", ke->d_name);
            tags[ntags].count = tag_count_file(kpath);
            ntags++;
        }
        closedir(pd);
    }

    qsort(tags, (size_t)ntags, sizeof(tags[0]), tag_cmp);
    for (i = 0; i < ntags; i++) {
        rc = cb(tags[i].key, tags[i].count, ctx);
        if (rc != 0)
            break;
    }

cleanup:
    free(tags);
    closedir(idx);
    return rc;
}
