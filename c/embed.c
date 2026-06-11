/* embed.c -- the FFI seam (see embed.h). Front-end glue, NOT engine internals:
 * it only adapts the engine's streaming callbacks into a single result buffer
 * for a host in a garbage-collected language (Dart/Swift/Kotlin). The engine
 * itself stays streaming and stack-bounded; the one heap buffer here lives at
 * the boundary, where a result set must be materialized for the caller. */
#define _POSIX_C_SOURCE 200809L     /* strtok_r */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "ais.h"
#include "common.h"
#include "embed.h"

void *ais_embed_open(const char *dir)
{
    ais *a;

    if (dir == NULL)
        return NULL;
    a = malloc(sizeof *a);
    if (a == NULL)
        return NULL;
    if (ais_open(a, dir) != 0) {
        free(a);
        return NULL;
    }
    return a;
}

void ais_embed_close(void *handle)
{
    ais *a = handle;

    if (a == NULL)
        return;
    ais_close(a);
    free(a);
}

long ais_embed_store(void *handle, const char *keys, const char *value)
{
    if (handle == NULL || keys == NULL || value == NULL)
        return -1;
    return ais_put((ais *)handle, keys, value);
}

void ais_embed_free(char *buf)
{
    free(buf);
}

/* ---- recall: gather "id|value\n" lines into a grown buffer --------------- */
struct buf { char *p; size_t len, cap; };

static int buf_add(struct buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 1024;
        char *q;
        while (cap < b->len + n + 1)
            cap *= 2;
        q = realloc(b->p, cap);
        if (q == NULL)
            return -1;
        b->p = q;
        b->cap = cap;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
    return 0;
}

struct rec_ctx { ais *a; struct buf *b; int oom; };

static int on_value(long id, const char *value, void *vp)
{
    struct rec_ctx *c = vp;
    char head[32];
    int n = snprintf(head, sizeof head, "%ld|", id);

    if (n < 0 || buf_add(c->b, head, (size_t)n) < 0 ||
        buf_add(c->b, value, strlen(value)) < 0 || buf_add(c->b, "\n", 1) < 0)
        c->oom = 1;
    return c->oom ? -1 : 0;
}

static int on_id(long id, void *vp)
{
    struct rec_ctx *c = vp;

    ais_record(c->a, id, on_value, c);
    return c->oom ? -1 : 0;
}

char *ais_embed_recall(void *handle, const char *keys, int or_mode)
{
    ais *a = handle;
    char kbuf[AIS_LINE_MAX];
    char *kv[AIS_KEYS_MAX];
    int nkeys = 0;
    char *tok, *save;
    struct buf b = { NULL, 0, 0 };
    struct rec_ctx c;

    if (a == NULL || keys == NULL || strlen(keys) >= sizeof kbuf)
        return NULL;
    strcpy(kbuf, keys);                 /* mutable copy: ais_get tokenizes in place */

    for (tok = strtok_r(kbuf, " ", &save); tok != NULL && nkeys < AIS_KEYS_MAX;
         tok = strtok_r(NULL, " ", &save))
        kv[nkeys++] = tok;

    c.a = a; c.b = &b; c.oom = 0;
    if (nkeys > 0)
        ais_get(a, kv, nkeys, or_mode ? AIS_OR : AIS_AND, on_id, &c);

    if (c.oom) {
        free(b.p);
        return NULL;
    }
    if (b.p == NULL) {                  /* no matches: an empty string, not NULL */
        b.p = malloc(1);
        if (b.p != NULL)
            b.p[0] = '\0';
    }
    return b.p;
}

/* Return an empty "" buffer instead of NULL so the host sees "no rows", not an
 * error; NULL is reserved for bad args / OOM. */
static char *buf_finish(struct buf *b, int oom)
{
    if (oom) {
        free(b->p);
        return NULL;
    }
    if (b->p == NULL) {
        b->p = malloc(1);
        if (b->p != NULL)
            b->p[0] = '\0';
    }
    return b->p;
}

/* ---- timeline / tags: same line formats as /api/timeline and /api/tags ---- */
struct emit_ctx { struct buf *b; int oom; };

static int tl_emit(long id, const char *ts, const char *keys,
                   const char *value, void *vp)
{
    struct emit_ctx *c = vp;
    char head[32];
    int n = snprintf(head, sizeof head, "%ld|", id);

    if (n < 0 || buf_add(c->b, head, (size_t)n) < 0 ||
        buf_add(c->b, ts, strlen(ts)) < 0     || buf_add(c->b, "|", 1) < 0 ||
        buf_add(c->b, keys, strlen(keys)) < 0 || buf_add(c->b, "|", 1) < 0 ||
        buf_add(c->b, value, strlen(value)) < 0 || buf_add(c->b, "\n", 1) < 0)
        c->oom = 1;
    return c->oom ? -1 : 0;
}

static int tag_emit(const char *key, long count, void *vp)
{
    struct emit_ctx *c = vp;
    char head[32];
    int n = snprintf(head, sizeof head, "%ld|", count);

    if (n < 0 || buf_add(c->b, head, (size_t)n) < 0 ||
        buf_add(c->b, key, strlen(key)) < 0 || buf_add(c->b, "\n", 1) < 0)
        c->oom = 1;
    return c->oom ? -1 : 0;
}

char *ais_embed_timeline(void *handle, int limit)
{
    struct buf b = { NULL, 0, 0 };
    struct emit_ctx c = { &b, 0 };

    if (handle == NULL)
        return NULL;
    ais_timeline((ais *)handle, limit, tl_emit, &c);
    return buf_finish(&b, c.oom);
}

char *ais_embed_tags(void *handle)
{
    struct buf b = { NULL, 0, 0 };
    struct emit_ctx c = { &b, 0 };

    if (handle == NULL)
        return NULL;
    ais_tags((ais *)handle, tag_emit, &c);
    return buf_finish(&b, c.oom);
}
