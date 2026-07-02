/* embed.c -- the FFI seam (see embed.h). Front-end glue, NOT engine internals:
 * it only adapts the engine's streaming callbacks into a single result buffer
 * for a host in a garbage-collected language (Dart/Swift/Kotlin). The engine
 * itself stays streaming and stack-bounded; the one heap buffer here lives at
 * the boundary, where a result set must be materialized for the caller. */
#define _POSIX_C_SOURCE 200809L     /* strtok_r */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>

#include "ais.h"
#include "common.h"
#include "doc.h"
#include "embed.h"
#include "locate.h"
#include "secret.h"
#include "sync.h"

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
    /* One value -> one record. A multi-line value becomes a blob-backed
     * document (see doc.c), exactly as the web and CLI front-ends do. */
    return ais_put_value((ais *)handle, keys, value);
}

long ais_embed_store_encrypted(void *handle, const char *keys,
                               const char *value, const char *passphrase)
{
    char marked[8192];
    long id;

    if (handle == NULL || value == NULL || passphrase == NULL)
        return -1;
    if (secret_encrypt((const unsigned char *)value, strlen(value),
                       (const unsigned char *)passphrase, strlen(passphrase),
                       marked, sizeof marked) < 0)
        return -1;                          /* crypto not built, or value too large */
    id = ais_put((ais *)handle, (keys != NULL) ? keys : "", marked);
    secret_wipe(marked, sizeof marked);
    return id;
}

char *ais_embed_reveal(const char *marked_value, const char *passphrase)
{
    unsigned char buf[AIS_LINE_MAX];
    long n;
    char *out = NULL;

    if (marked_value == NULL || passphrase == NULL)
        return NULL;
    n = secret_decrypt(marked_value, (const unsigned char *)passphrase,
                       strlen(passphrase), buf, sizeof buf);
    if (n >= 0) {
        out = malloc((size_t)n + 1);
        if (out != NULL) {
            memcpy(out, buf, (size_t)n);
            out[n] = '\0';
        }
    }
    secret_wipe(buf, sizeof buf);          /* don't leave the cleartext on the stack */
    return out;
}

int ais_embed_del(void *handle, long id)
{
    if (handle == NULL)
        return -1;
    return ais_del((ais *)handle, id);
}

int ais_embed_update(void *handle, long id, const char *keys)
{
    if (handle == NULL || keys == NULL)
        return -1;
    return ais_update((ais *)handle, id, keys);
}

int ais_embed_pull(void *handle, const char *url, const char *token)
{
    ais *a = handle;
    char host[128];
    int port;

    if (a == NULL || url == NULL || token == NULL)
        return -1;                          /* bad args */
    signal(SIGPIPE, SIG_IGN);               /* a dropped peer must not kill the host app */
    if (sync_parse_url(url, host, sizeof host, &port) != 0)
        return -1;                          /* malformed url */
    if (sync_pull(a, host, port, token, 10) != 0)   /* 10s LAN timeout */
        return -2;                          /* unreachable, wrong token, or timeout */
    return 0;                               /* merged */
}

int ais_embed_serve(void *handle, int port, const char *token)
{
    ais *a = handle;

    if (a == NULL || token == NULL)
        return -1;                          /* bad args */
    signal(SIGPIPE, SIG_IGN);               /* a peer that vanishes mid-write must not kill the app */
    if (sync_serve(a, port, token, 120) != 0)   /* wait up to 120s for one peer */
        return -2;                          /* no peer completed: timeout, wrong token, or error */
    return 0;                               /* a peer pulled and merged */
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
    /* or_mode is the mode switch: 0 = AND (intersection), 1 = OR (union). The
     * GUI's "Match any key" box picks it; no automatic relaxation. */
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

char *ais_embed_timeline(void *handle, long before_id, int count,
                         const char *from, const char *to)
{
    struct buf b = { NULL, 0, 0 };
    struct emit_ctx c = { &b, 0 };

    if (handle == NULL)
        return NULL;
    ais_timeline((ais *)handle, before_id, count,
                 from ? from : "", to ? to : "", tl_emit, &c);
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

/* Persist DIR as the saved default index (~/.ais/config), for a GUI's "change
 * store" so the choice sticks next run. 0 on success, -1 on failure. */
int ais_embed_default_set(const char *dir)
{
    return ais_default_set(dir);
}

/* Resolve the same index the CLI would open with no -f (see embed.h). */
int ais_embed_locate(char *out, size_t outsz)
{
    return ais_locate(NULL, out, outsz);
}
