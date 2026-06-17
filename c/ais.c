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

/* Post each whitespace-separated key of KEYS to record ID. A bare token is
 * ATTACHED (post_insert keeps each posting ascending and duplicate-free, so
 * re-puts add nothing); a "-key" token is DETACHED -- the posting is removed and
 * the (id,key) pair recorded in ktomb, so the record keeps its id but loses the
 * key (re-attaching the same key clears the pair). Returns 0/-1. */
static int ais_post_keys(ais *a, const char *keys, long id)
{
    char buf[AIS_LINE_MAX];
    char *tok, *save;
    int n, active;

    n = snprintf(buf, sizeof(buf), "%s", keys);
    if (n < 0 || (size_t)n >= sizeof(buf))
        return -1;
    active = ktomb_active(a);            /* gate the re-attach cleanup (cheap) */
    if (active < 0)
        return -1;
    for (tok = strtok_r(buf, " \t", &save); tok != NULL;
         tok = strtok_r(NULL, " \t", &save)) {
        if (tok[0] == '-' && tok[1] != '\0') {       /* "-key": detach */
            const char *k = tok + 1;
            int had = ktomb_contains(a, id, k);      /* avoid duplicate entries */
            if (had < 0)
                return -1;
            if (post_remove(a, k, id) != 0)
                return -1;
            if (!had && ktomb_append(a, id, k) != 0)
                return -1;
        } else {                                     /* attach */
            if (post_insert(a, tok, id) != 0)
                return -1;
            if (active && ktomb_remove(a, id, tok) != 0)
                return -1;                           /* clear any prior detach */
        }
    }
    return 0;
}

/* Copy KEYS into OUT keeping only attach tokens; a "-key" (detach) is dropped,
 * since a brand-new record has no key to detach. Returns 0/-1 (too long). */
static int keys_attach_only(const char *keys, char *out, size_t outsz)
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
        if (tok[0] == '-' && tok[1] != '\0')
            continue;                    /* a detach token: never stored */
        n = snprintf(out + used, outsz - used, "%s%s", used ? " " : "", tok);
        if (n < 0 || used + (size_t)n >= outsz)
            return -1;
        used += (size_t)n;
    }
    return 0;
}

long ais_put(ais *a, const char *keys, const char *value)
{
    long id = 0, rc;
    int found;
    char clean[AIS_LINE_MAX];

    /* A save only ATTACHES keys; detaching a key is ais_update's job (it needs
     * the id as the handle). So drop any "-key" tokens here. */
    if (keys_attach_only(keys, clean, sizeof clean) != 0)
        return -1;

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
        rc = (ais_post_keys(a, clean, id) != 0) ? -1 : id;
        goto out;
    }

    id = a->next_id;
    {
        char ts[AIS_TS_MAX];
        long off = store_bytes(a);          /* offset the new line gets */
        int  ok  = (off >= 0) && off_consistent(a);
        store_now(ts, sizeof(ts));          /* "" if the clock is unreadable */
        if (store_append(a, id, ts, clean, value) != 0) { rc = -1; goto out; }
        if (ok && off_append(a, off) != 0)             { rc = -1; goto out; }  /* keep "off" in lockstep */
    }
    if (ais_post_keys(a, clean, id) != 0) { rc = -1; goto out; }
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

/* Edit the keys of an existing record: each bare token in KEYS is attached, each
 * "-key" detached (see ais_post_keys). The record's id and value are unchanged.
 * The id is the handle (from any get/--find/--dump/--timeline line "id|value"),
 * which is why this is keyed by id and not by re-typing the value. Returns 0, or
 * -1 if the id is unknown, deleted, or on error. */
int ais_update(ais *a, long id, const char *keys)
{
    struct add_lookup L;
    int rc = -1, t;

    if (store_wlock(a) != 0)
        return -1;

    t = tomb_contains(a, id);
    if (t != 0)                         /* 1 = deleted, -1 = error: refuse */
        goto out;

    L.id = id;
    L.found = 0;
    L.keys[0] = '\0';
    if (store_each_record(a, add_seek, &L) < -1)   /* real error (cb stops at -1) */
        goto out;
    if (!L.found)                       /* no such id */
        goto out;

    rc = (ais_post_keys(a, keys, id) != 0) ? -1 : 0;
out:
    store_wunlock(a);
    if (rc == 0)
        debug("update: id=%ld keys='%s'", id, keys);
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

/* Write KEYS minus any key detached from ID (per ktomb) into OUT (size OUTSZ).
 * Tokens are space-separated, order preserved; truncates to fit (display use).
 * Only called when the ktomb is active. Returns 0/-1. */
static int keys_visible(ais *a, long id, const char *keys, char *out, size_t outsz)
{
    char buf[AIS_LINE_MAX];
    char *tok, *save;
    size_t used = 0;
    int n;

    if (snprintf(buf, sizeof(buf), "%s", keys) >= (int)sizeof(buf))
        return -1;
    out[0] = '\0';
    for (tok = strtok_r(buf, " \t", &save); tok != NULL;
         tok = strtok_r(NULL, " \t", &save)) {
        int t = ktomb_contains(a, id, tok);
        if (t < 0)
            return -1;
        if (t == 1)
            continue;                          /* detached: hide */
        n = snprintf(out + used, outsz - used, "%s%s", used ? " " : "", tok);
        if (n < 0)
            return -1;
        if (used + (size_t)n >= outsz)
            break;                             /* truncate to fit */
        used += (size_t)n;
    }
    return 0;
}

/* Context for ais_dump: print live store lines (tombstones merged out). */
struct dump_ctx {
    ais  *a;
    FILE *out;
    int   filter;     /* 1 if ktomb active: hide detached keys */
};

static int dump_line(long id, const char *ts, const char *keys,
                     const char *value, void *vp)
{
    struct dump_ctx *D = vp;
    int t = tomb_contains(D->a, id);
    char vis[AIS_LINE_MAX];
    const char *k = keys;
    (void)ts;   /* dump is the content serialization; id and ts are reassigned
                 * on re-import, so dump stays id|keys|value (see feed_import) */
    if (t < 0)
        return -1;
    if (t != 0)
        return 0;
    if (D->filter) {
        if (keys_visible(D->a, id, keys, vis, sizeof(vis)) != 0)
            return -1;
        k = vis;
    }
    fprintf(D->out, "%ld|%s|%s\n", id, k, value);
    return 0;
}

void ais_dump(ais *a, FILE *out)
{
    struct dump_ctx D;
    D.a = a;
    D.out = out;
    D.filter = (ktomb_active(a) == 1);
    store_each_record(a, dump_line, &D);
}

/* --- timeline: keyset (cursor) pagination, newest id first -----------------
 * Scalable by design: with the "off" id->offset index we SEEK straight to each
 * record by id and read only the page asked for -- never the whole store. A
 * page is the COUNT live records with id < BEFORE_ID (BEFORE_ID <= 0 = from the
 * newest). "Load more" passes the last id shown as the next BEFORE_ID. Order is
 * id-descending (= reverse insertion ~= reverse chronological); one row per
 * record (its first/canonical line). When "off" is absent/stale (a legacy index
 * before --compact) it falls back to a bounded scan -- correct, not scalable. */
#define AIS_TL_DEFAULT  500    /* page size when COUNT <= 0                      */
#define AIS_TL_MAX    10000    /* hard cap on one page (bounds the fallback heap)*/
#define AIS_TL_VAL_MAX 2048    /* value snippet held per row in the fallback     */

/* Forward a record to the caller's cb -- hiding ktomb-detached keys, and only if
 * its date falls in the [from,to] range. Counts the rows actually emitted, so
 * the seek loop pages by in-range records, not by ids scanned. */
struct tl_emit {
    ais_tl_cb   cb;
    void       *ctx;
    ais        *a;
    int         filter;
    const char *from, *to;   /* "YYYY-MM-DD" bounds; "" / NULL = open-ended */
    int         emitted;
};

/* Is TS's date (its first 10 chars) within [FROM,TO]? An empty bound is open; a
 * dateless record (TS == "") is in range only when there is NO bound at all. */
static int tl_in_range(const char *ts, const char *from, const char *to)
{
    char d[11];
    int i;

    if ((from == NULL || from[0] == '\0') && (to == NULL || to[0] == '\0'))
        return 1;                            /* no date filter */
    if (ts[0] == '\0')
        return 0;                            /* dateless: excluded by a range */
    for (i = 0; i < 10 && ts[i] != '\0'; i++)
        d[i] = ts[i];
    d[i] = '\0';
    if (from != NULL && from[0] != '\0' && strcmp(d, from) < 0)
        return 0;
    if (to != NULL && to[0] != '\0' && strcmp(d, to) > 0)
        return 0;
    return 1;
}

static int tl_emit_one(long id, const char *ts, const char *keys,
                       const char *value, void *vp)
{
    struct tl_emit *e = vp;
    char vis[AIS_LINE_MAX];
    const char *k = keys;

    if (!tl_in_range(ts, e->from, e->to))
        return 0;                            /* out of range: skip, do not count */
    if (e->filter) {
        if (keys_visible(e->a, id, keys, vis, sizeof(vis)) != 0)
            return -1;
        k = vis;
    }
    e->emitted++;
    return e->cb(id, ts, k, value, e->ctx);
}

/* --- fallback (no usable "off"): one bounded scan keeping the COUNT highest
 * live ids below BOUND, one row per id, emitted id-descending. --------------- */
struct tl_entry {
    long id;
    char ts[AIS_TS_MAX];
    char keys[AIS_KEY_MAX];
    char value[AIS_TL_VAL_MAX];
};

struct tl_scan_ctx {
    ais             *a;
    long             bound;   /* keep only ids strictly below this              */
    struct tl_entry *top;     /* the COUNT highest qualifying ids (unsorted)    */
    int              cap, n;
    const char      *from, *to;   /* date range, "" = open                      */
};

static int tl_scan_collect(long id, const char *ts, const char *keys,
                           const char *value, void *vp)
{
    struct tl_scan_ctx *s = vp;
    int i, slot, t;

    if (id >= s->bound)
        return 0;
    if (!tl_in_range(ts, s->from, s->to))
        return 0;                 /* out of the date range */
    t = tomb_contains(s->a, id);
    if (t < 0)
        return -1;
    if (t == 1)
        return 0;                 /* deleted */
    for (i = 0; i < s->n; i++)
        if (s->top[i].id == id)
            return 0;             /* one row per id (keep the first line seen) */

    if (s->n < s->cap) {
        slot = s->n++;
    } else {                      /* full: replace the smallest id, if we beat it */
        slot = 0;
        for (i = 1; i < s->n; i++)
            if (s->top[i].id < s->top[slot].id)
                slot = i;
        if (id <= s->top[slot].id)
            return 0;
    }
    s->top[slot].id = id;
    snprintf(s->top[slot].ts,    sizeof(s->top[slot].ts),    "%s", ts);
    snprintf(s->top[slot].keys,  sizeof(s->top[slot].keys),  "%s", keys);
    snprintf(s->top[slot].value, sizeof(s->top[slot].value), "%s", value);
    return 0;
}

static int tl_id_desc(const void *pa, const void *pb)
{
    const struct tl_entry *a = pa, *b = pb;
    return (a->id < b->id) ? 1 : (a->id > b->id) ? -1 : 0;
}

static int tl_scan(ais *a, long before_id, int count, struct tl_emit *e)
{
    struct tl_scan_ctx s;
    int i, rc = 0;

    s.a = a;
    s.bound = (before_id > 0) ? before_id : a->next_id;   /* next_id > every id */
    s.cap = count;
    s.n = 0;
    s.from = e->from;
    s.to = e->to;
    s.top = malloc((size_t)count * sizeof(s.top[0]));
    if (s.top == NULL)
        return -1;
    if (store_each_record(a, tl_scan_collect, &s) < 0) {
        free(s.top);
        return -1;
    }
    qsort(s.top, (size_t)s.n, sizeof(s.top[0]), tl_id_desc);
    for (i = 0; i < s.n; i++) {
        rc = tl_emit_one(s.top[i].id, s.top[i].ts, s.top[i].keys, s.top[i].value, e);
        if (rc != 0)
            break;
    }
    free(s.top);
    return (rc < 0) ? -1 : 0;
}

int ais_timeline(ais *a, long before_id, int count,
                 const char *from, const char *to, ais_tl_cb cb, void *ctx)
{
    struct tl_emit e;
    long id, maxid;

    if (count <= 0)
        count = AIS_TL_DEFAULT;
    if (count > AIS_TL_MAX)
        count = AIS_TL_MAX;
    e.cb = cb; e.ctx = ctx; e.a = a;
    e.filter = (ktomb_active(a) == 1);
    e.from = from; e.to = to; e.emitted = 0;

    if (off_consistent(a) != 1)               /* no usable index: bounded scan */
        return tl_scan(a, before_id, count, &e);

    /* seek path: walk ids downward, reading only the page (the older records
     * are never touched) -- the scalable case. Counting is by in-range rows
     * (tl_emit_one increments e.emitted only for records the range admits). */
    maxid = a->next_id - 1;
    id = (before_id > 0 && before_id - 1 < maxid) ? before_id - 1 : maxid;
    for (; id >= 1 && e.emitted < count; id--) {
        long offset;
        int t = tomb_contains(a, id);
        if (t < 0)
            return -1;
        if (t == 1)
            continue;                          /* deleted */
        if (off_get(a, id, &offset) != 1)
            continue;                          /* gap / sentinel / short off */
        if (store_record_at(a, id, offset, tl_emit_one, &e) < 0)
            return -1;
    }
    return 0;
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
