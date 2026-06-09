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
    long id = 0;
    int found;

    found = store_find_value(a, value, &id);
    if (found < 0)
        return -1;

    if (found) {
        /* value already stored: reuse its id, just (re)post the keys. Posting
         * is idempotent at the merge level only by dedup -- but re-posting an
         * existing id would duplicate it in the file. We avoid that by posting
         * keys only for a genuinely new value below; for an existing value the
         * keys were filed when it was first put. New keys on an existing value
         * are handled by adding them here, tolerating that the merge dedups. */
        debug("put: value exists as id=%ld (idempotent)", id);
        if (ais_post_keys(a, keys, id) != 0)
            return -1;
        return id;
    }

    id = a->next_id;
    if (store_append(a, id, keys, value) != 0)
        return -1;
    if (ais_post_keys(a, keys, id) != 0)
        return -1;
    a->next_id = id + 1;
    if (store_save_next_id(a) != 0)
        return -1;
    debug("put: new id=%ld", id);
    return id;
}

/* Context for ais_add: find whether the id exists and capture its keys. */
struct add_lookup {
    long  id;
    int   found;
    char  keys[AIS_LINE_MAX];
};

static int add_seek(long id, const char *keys, const char *value, void *vp)
{
    struct add_lookup *L = vp;
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
    int rc;

    L.id = id;
    L.found = 0;
    L.keys[0] = '\0';

    rc = store_each_record(a, add_seek, &L);
    if (rc < -1)            /* a real error (callback stops with -1) */
        return -1;
    if (!L.found)
        return -1;

    /* continuation line: same id, same keys field, the new value. Keys are
     * already posted to this id, so no re-post. */
    if (store_append(a, id, L.keys, value) != 0)
        return -1;
    debug("add: appended link to id=%ld", id);
    return 0;
}

int ais_del(ais *a, long id)
{
    if (tomb_append(a, id) != 0)
        return -1;
    debug("del: tombstoned id=%ld", id);
    return 0;
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

static int record_seek(long id, const char *keys, const char *value, void *vp)
{
    struct record_ctx *R = vp;
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

static int dump_line(long id, const char *keys, const char *value, void *vp)
{
    struct dump_ctx *D = vp;
    int t = tomb_contains(D->a, id);
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
