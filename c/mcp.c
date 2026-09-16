/* mcp.c -- MCP server over stdin/stdout. See mcp.h.
 *
 * Self-contained on purpose: it calls the engine's public API and nothing else
 * in this directory, so adding or removing this file changes nothing but
 * main.c's dispatch. The mobile builds exclude it by name (ais_engine.podspec,
 * app/flutter/src/CMakeLists.txt): a phone has no stdin to serve.
 *
 * stdout carries protocol and nothing else, so diagnostics go to stderr.
 *
 * Replies STREAM. A record is escaped into the reply as the engine hands it
 * over, so recalling the whole index costs the same memory as recalling one
 * record, and the reply is never assembled anywhere. The price is that a reply
 * cannot be retracted once it has started, so everything that can fail (the
 * arguments, a temporary file) is settled before the first byte goes out.
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "ais.h"
#include "doc.h"       /* ais_put_value: one record, blob-backed if multi-line */
#include "find.h"      /* ais_find: the content search behind the find tool    */
#include "mcp.h"

#define MCP_REQ_MAX   AIS_LINE_MAX  /* one request line; longer is refused     */
#define MCP_NODES     256           /* JSON values in one request              */
#define MCP_ROWS_DEF  200           /* rows a tool returns when none is asked  */
#define MCP_PROTOCOL  "2025-06-18"  /* answered unless the client names a known */
                                    /* revision: 2024-11-05, 2025-03-26, this   */

/* ---- reading JSON -------------------------------------------------------
 * Enough of RFC 8259 to read one JSON-RPC request. Strings are unescaped IN
 * PLACE inside the request buffer (unescaping never grows a string), and the
 * values live in one fixed node table, so a request costs no allocation and
 * bounded memory whatever the client sends. */

enum { JNONE = 0, JOBJ, JARR, JSTR, JNUM, JLIT };

typedef struct {
    char  type;
    char  isint;      /* JNUM: the token has no fraction and no exponent */
    char *key;        /* member name, when the parent is an object */
    char *str;        /* JSTR: the unescaped text; JNUM: the raw token */
    long  num;        /* JNUM: the raw token's length */
    long  val;        /* JNUM: the integer part; JLIT: 1 for true */
    int   kid, sib;   /* first child, next sibling; -1 = none */
} jnode;

typedef struct { jnode v[MCP_NODES]; int n; } jdoc;

static void jskip(char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')
        (*p)++;
}

static int jnew(jdoc *d, int type)
{
    jnode *v;

    if (d->n >= MCP_NODES)
        return -1;                       /* a request deeper than we will read */
    v = &d->v[d->n];
    v->type = (char)type; v->isint = 0; v->key = NULL; v->str = NULL;
    v->num = 0; v->val = 0; v->kid = -1; v->sib = -1;
    return d->n++;
}

/* One code point as UTF-8. A \uXXXX escape is 6 bytes in and at most 4 out, so
 * this can never outrun the text it is rewriting. */
static char *jutf8(char *w, unsigned long cp)
{
    if (cp < 0x80UL) {
        *w++ = (char)cp;
    } else if (cp < 0x800UL) {
        *w++ = (char)(0xC0 | (cp >> 6));
        *w++ = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000UL) {
        *w++ = (char)(0xE0 | (cp >> 12));
        *w++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *w++ = (char)(0x80 | (cp & 0x3F));
    } else {
        *w++ = (char)(0xF0 | (cp >> 18));
        *w++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *w++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *w++ = (char)(0x80 | (cp & 0x3F));
    }
    return w;
}

static int jhex4(char **p, unsigned long *out)
{
    unsigned long v = 0;
    int i;

    for (i = 0; i < 4; i++) {
        int c = (unsigned char)*(*p)++;
        if (c >= '0' && c <= '9')      v = v * 16 + (unsigned long)(c - '0');
        else if (c >= 'a' && c <= 'f') v = v * 16 + (unsigned long)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = v * 16 + (unsigned long)(c - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

/* Unescape the string at *P in place, leaving *P after the closing quote.
 * Returns the text, or NULL if it is malformed. */
static char *jstring(char **p)
{
    char *r = *p, *w, *start;

    if (*r != '"')
        return NULL;
    r++;
    start = r;
    w = r;
    while (*r != '"') {
        unsigned long cp, lo;
        if (*r == '\0' || (unsigned char)*r < 0x20)
            return NULL;                 /* unterminated, or a raw control byte */
        if (*r != '\\') { *w++ = *r++; continue; }
        r++;
        switch (*r) {
        case '"': case '\\': case '/': *w++ = *r++; break;
        case 'b': *w++ = '\b'; r++; break;
        case 'f': *w++ = '\f'; r++; break;
        case 'n': *w++ = '\n'; r++; break;
        case 'r': *w++ = '\r'; r++; break;
        case 't': *w++ = '\t'; r++; break;
        case 'u':
            r++;
            if (jhex4(&r, &cp) != 0)
                return NULL;
            if (cp >= 0xD800UL && cp <= 0xDBFFUL) {      /* a surrogate pair */
                if (r[0] == '\\' && r[1] == 'u') {
                    char *save = r;
                    r += 2;
                    if (jhex4(&r, &lo) != 0)
                        return NULL;
                    if (lo >= 0xDC00UL && lo <= 0xDFFFUL)
                        cp = 0x10000UL + ((cp - 0xD800UL) << 10) + (lo - 0xDC00UL);
                    else { r = save; cp = 0xFFFDUL; }    /* lone high surrogate */
                } else {
                    cp = 0xFFFDUL;
                }
            } else if (cp >= 0xDC00UL && cp <= 0xDFFFUL) {
                cp = 0xFFFDUL;                            /* lone low surrogate */
            } else if (cp == 0) {
                cp = 0xFFFDUL;         /* a real NUL would end the string here */
            }
            w = jutf8(w, cp);
            break;
        default:
            return NULL;
        }
    }
    *w = '\0';            /* lands on the closing quote or earlier: never past */
    *p = r + 1;
    return start;
}

static int jparse(jdoc *d, char **p)
{
    int me, kid, last = -1;
    char *k;

    jskip(p);
    switch (**p) {
    case '{':
        me = jnew(d, JOBJ);
        if (me < 0) return -1;
        (*p)++;
        jskip(p);
        if (**p == '}') { (*p)++; return me; }
        for (;;) {
            jskip(p);
            k = jstring(p);
            if (k == NULL) return -1;
            jskip(p);
            if (*(*p)++ != ':') return -1;
            kid = jparse(d, p);
            if (kid < 0) return -1;
            d->v[kid].key = k;
            if (last < 0) d->v[me].kid = kid; else d->v[last].sib = kid;
            last = kid;
            jskip(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == '}') { (*p)++; return me; }
            return -1;
        }
    case '[':
        me = jnew(d, JARR);
        if (me < 0) return -1;
        (*p)++;
        jskip(p);
        if (**p == ']') { (*p)++; return me; }
        for (;;) {
            kid = jparse(d, p);
            if (kid < 0) return -1;
            if (last < 0) d->v[me].kid = kid; else d->v[last].sib = kid;
            last = kid;
            jskip(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == ']') { (*p)++; return me; }
            return -1;
        }
    case '"':
        me = jnew(d, JSTR);
        if (me < 0) return -1;
        d->v[me].str = jstring(p);
        return (d->v[me].str == NULL) ? -1 : me;
    case 't': case 'f': case 'n': {
        static const char *const lit[] = { "true", "false", "null" };
        int i;
        for (i = 0; i < 3; i++) {
            size_t n = strlen(lit[i]);
            if (strncmp(*p, lit[i], n) == 0) {
                me = jnew(d, JLIT);
                if (me < 0) return -1;
                d->v[me].val = (i == 0);
                if (i != 2)
                    d->v[me].str = (char *)lit[i];   /* NULL str marks the null */
                *p += n;
                return me;
            }
        }
        return -1;
    }
    default: {
        char *start = *p, *end = *p;
        int frac = 0;

        /* RFC 8259's number, not strtol's: strtol reads '+5' and '007', which
         * are not JSON, and an id echoed back as the bytes that arrived would
         * then make the reply unparseable. A token that fails the grammar is a
         * parse error for the whole message. */
        if (*end == '-')
            end++;
        if (*end == '0')
            end++;
        else if (*end >= '1' && *end <= '9')
            while (*end >= '0' && *end <= '9') end++;
        else
            return -1;
        if (*end == '.') {
            end++;
            if (*end < '0' || *end > '9') return -1;
            while (*end >= '0' && *end <= '9') end++;
            frac = 1;
        }
        if (*end == 'e' || *end == 'E') {
            end++;
            if (*end == '+' || *end == '-') end++;
            if (*end < '0' || *end > '9') return -1;
            while (*end >= '0' && *end <= '9') end++;
            frac = 1;
        }
        me = jnew(d, JNUM);
        if (me < 0) return -1;
        /* Three readings, all wanted: the integer for a limit, whether it IS an
         * integer, and the token itself for an id, which goes back unchanged
         * whatever it was. A token past LONG_MAX saturates here and is refused
         * wherever a number has a range. */
        d->v[me].val = strtol(start, NULL, 10);
        d->v[me].isint = (char)!frac;
        d->v[me].str = start;
        d->v[me].num = (long)(end - start);
        *p = end;
        return me;
    }
    }
}

static int jget(const jdoc *d, int obj, const char *key)
{
    int i;

    if (obj < 0 || d->v[obj].type != JOBJ)
        return -1;
    for (i = d->v[obj].kid; i >= 0; i = d->v[i].sib)
        if (d->v[i].key != NULL && strcmp(d->v[i].key, key) == 0)
            return i;
    return -1;
}

static const char *jtext(const jdoc *d, int i)
{
    return (i >= 0 && d->v[i].type == JSTR) ? d->v[i].str : NULL;
}

static long jint(const jdoc *d, int i, long dflt)
{
    return (i >= 0 && d->v[i].type == JNUM) ? d->v[i].val : dflt;
}

/* ---- writing JSON ------------------------------------------------------- */

/* S as the BODY of a JSON string: the escapes RFC 8259 demands and no others,
 * so UTF-8 passes through as the bytes the store holds. */
static void jout(const char *s)
{
    const unsigned char *u = (const unsigned char *)s;

    while (*u != '\0') {
        unsigned char c = *u;
        int n, i;

        switch (c) {
        case '"':  fputs("\\\"", stdout); u++; continue;
        case '\\': fputs("\\\\", stdout); u++; continue;
        case '\n': fputs("\\n", stdout);  u++; continue;
        case '\r': fputs("\\r", stdout);  u++; continue;
        case '\t': fputs("\\t", stdout);  u++; continue;
        default: break;
        }
        if (c < 0x20)  { printf("\\u%04x", (unsigned)c); u++; continue; }
        if (c < 0x80)  { putchar((int)c); u++; continue; }
        /* A stored value can hold any byte: the CLI, --import and sync take
         * what they are given, and a Latin-1 paste is one command away. JSON
         * must be UTF-8, and a client decodes the LINE before parsing it, so
         * one stray byte would cost the whole reply rather than one row.
         * Anything malformed goes out as U+FFFD and the rest survives. */
        n = (c >= 0xF0 && c <= 0xF4) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC2) ? 1 : -1;
        for (i = 1; i <= n; i++)
            if ((u[i] & 0xC0) != 0x80) { n = -1; break; }
        if (n > 0 && ((c == 0xE0 && u[1] < 0xA0) ||     /* overlong */
                      (c == 0xED && u[1] > 0x9F) ||     /* a surrogate */
                      (c == 0xF0 && u[1] < 0x90) ||     /* overlong */
                      (c == 0xF4 && u[1] > 0x8F)))      /* past U+10FFFF */
            n = -1;
        if (n < 0) { fputs("\\ufffd", stdout); u++; continue; }
        fwrite(u, 1, (size_t)n + 1, stdout);
        u += n + 1;
    }
}

/* The request's id, echoed back as the bytes that arrived. JSON-RPC lets it be
 * a number or a string, and a client that cannot match the id it sent drops the
 * reply and waits, so a number is never re-rendered from a long: 1.5 and an id
 * past LONG_MAX both have to come back as themselves.
 * ID_S is the text (for a string) or the raw token (for a number); NULL with
 * HAS_ID set is the literal null, which JSON-RPC calls a request, not a
 * notification. BAD is an id of any other type: answered with id null, since
 * echoing an object or an array back would be a second invalid request. */
struct req { int has_id, id_str, bad; const char *id_s; size_t id_raw; };

static void req_id(const jdoc *d, int root, struct req *q)
{
    int i = jget(d, root, "id");

    q->has_id = 0; q->id_str = 0; q->bad = 0; q->id_s = NULL; q->id_raw = 0;
    if (i < 0)
        return;                                   /* no id at all: a notification */
    if (d->v[i].type == JSTR) {
        q->has_id = 1; q->id_str = 1; q->id_s = d->v[i].str;
    } else if (d->v[i].type == JNUM) {
        q->has_id = 1; q->id_s = d->v[i].str; q->id_raw = (size_t)d->v[i].num;
    } else if (d->v[i].type == JLIT && d->v[i].str == NULL) {
        q->has_id = 1;                            /* id: null, answered with null */
    } else {
        q->bad = 1;                    /* an object, an array or true/false */
    }
}

static void reply_head(const struct req *q)
{
    fputs("{\"jsonrpc\":\"2.0\",\"id\":", stdout);
    if (q == NULL || !q->has_id || q->id_s == NULL) fputs("null", stdout);
    else if (q->id_str)  { putchar('"'); jout(q->id_s); putchar('"'); }
    else                 fwrite(q->id_s, 1, q->id_raw, stdout);
}

static void reply_end(void)
{
    fputs("}\n", stdout);
    fflush(stdout);
}

static void reply_error(const struct req *q, int code, const char *msg)
{
    reply_head(q);
    printf(",\"error\":{\"code\":%d,\"message\":\"", code);
    jout(msg);
    fputs("\"}", stdout);
    reply_end();
}

/* A tool reply is one text block. Opened before the rows stream out, closed
 * after; IS_ERROR marks a failure the model should read rather than a
 * protocol fault (an unknown tool is the latter, a refused write the former). */
static void text_open(const struct req *q)
{
    reply_head(q);
    fputs(",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"", stdout);
}

static void text_close(int is_error)
{
    fputs("\"}]", stdout);
    if (is_error)
        fputs(",\"isError\":true", stdout);
    fputs("}", stdout);
    reply_end();
}

static void text_reply(const struct req *q, const char *msg, int is_error)
{
    text_open(q);
    jout(msg);
    text_close(is_error);
}

/* ---- the tools ----------------------------------------------------------
 * Each streams "one row per line" in the shape the CLI prints, because that is
 * the contract every other front end already follows. */

struct sink { ais *a; long left; long rows; };

static int on_value(long id, const char *value, void *vp)
{
    struct sink *s = vp;
    char idbuf[32];

    if (s->left <= 0)
        return -1;                               /* the caller's row budget */
    snprintf(idbuf, sizeof idbuf, "%ld|", id);
    jout(idbuf);
    jout(value);
    jout("\n");
    s->left--;
    s->rows++;
    return 0;
}

static int on_id(long id, void *vp)
{
    struct sink *s = vp;

    ais_record(s->a, id, on_value, s);
    return (s->left <= 0) ? -1 : 0;
}

static int on_tag(const char *key, long count, void *vp)
{
    struct sink *s = vp;
    char cbuf[32];

    if (s->left <= 0)
        return -1;
    snprintf(cbuf, sizeof cbuf, "%ld|", count);
    jout(cbuf);
    jout(key);
    jout("\n");
    s->left--;
    s->rows++;
    return 0;
}

static int on_tl(long id, const char *ts, const char *keys, const char *value, void *vp)
{
    struct sink *s = vp;
    char idbuf[32];

    if (s->left <= 0)
        return -1;
    snprintf(idbuf, sizeof idbuf, "%ld|", id);
    jout(idbuf);
    jout(ts);   jout("|");
    jout(keys); jout("|");
    jout(value);
    jout("\n");
    s->left--;
    s->rows++;
    return 0;
}

/* Split S on blanks in place, appending each word to KV. Spelled out rather
 * than strtok_r, which needs a feature macro on some systems: this file should
 * compile with nothing but C99. Returns how many words were added. */
static int split_keys(char *s, char *kv[], int max)
{
    int n = 0;

    for (;;) {
        while (*s == ' ' || *s == '\t')
            s++;
        if (*s == '\0' || n >= max)
            return n;
        kv[n++] = s;
        while (*s != '\0' && *s != ' ' && *s != '\t')
            s++;
        if (*s != '\0')
            *s++ = '\0';
    }
}

/* KEYS may arrive as an array of strings or as one space-separated string;
 * either way the words are the keys. The strings point into the request line,
 * which is ours to cut up. Returns how many keys landed in KV. */
static int keys_arg(const jdoc *d, int node, char *kv[], int max)
{
    int n = 0, i;

    if (node < 0)
        return 0;
    if (d->v[node].type == JSTR)
        return split_keys(d->v[node].str, kv, max);
    if (d->v[node].type != JARR)
        return 0;
    for (i = d->v[node].kid; i >= 0 && n < max; i = d->v[i].sib)
        if (d->v[i].type == JSTR)
            n += split_keys(d->v[i].str, kv + n, max - n);
    return n;
}

/* A row budget from the arguments. Out of int range it would reach ais_get_page
 * as a wrapped value, and 0 there means UNBOUNDED: the opposite of a limit. */
static long rows_arg(const jdoc *d, int args, const char *name)
{
    long n = jint(d, jget(d, args, name), MCP_ROWS_DEF);

    if (n <= 0)
        return MCP_ROWS_DEF;
    return (n > INT_MAX) ? INT_MAX : n;
}

static void sink_init(struct sink *s, ais *a, long left)
{
    s->a = a; s->left = left; s->rows = 0;
}

/* An empty result is a fact, not a failure: say so in words, or a model reads
 * an empty block as a broken tool and tries again. A spent budget is the other
 * half of the same problem: unmarked, a page reads as the whole index. */
static void rows_done(const struct sink *s)
{
    if (s->rows == 0)
        jout("no match");
    else if (s->left == 0)
        jout("(stopped at the limit; there may be more)\n");
}

static void tool_recall(ais *a, const jdoc *d, int args, const struct req *q)
{
    char *kv[AIS_KEYS_MAX];
    const char *match;
    struct sink s;
    int nkeys;

    nkeys = keys_arg(d, jget(d, args, "keys"), kv, AIS_KEYS_MAX);
    if (nkeys == 0) {
        text_reply(q, "recall needs at least one key", 1);
        return;
    }
    match = jtext(d, jget(d, args, "match"));
    sink_init(&s, a, rows_arg(d, args, "limit"));
    text_open(q);
    ais_get_page(a, kv, nkeys, (match != NULL && strcmp(match, "any") == 0) ? AIS_OR : AIS_AND,
                 0, (int)s.left, on_id, &s);
    rows_done(&s);
    text_close(0);
}

static void tool_find(ais *a, const jdoc *d, int args, const struct req *q)
{
    const char *text = jtext(d, jget(d, args, "text"));
    char line[AIS_LINE_MAX];
    long left, want;
    FILE *tmp;

    if (text == NULL || *text == '\0') {
        text_reply(q, "find needs text", 1);
        return;
    }
    /* ais_find prints to a stream, so it lands in a temporary one and is
     * escaped back out: opened BEFORE the reply, since a failure here still
     * has somewhere to go. */
    tmp = tmpfile();
    if (tmp == NULL) {
        text_reply(q, "find: no temporary file", 1);
        return;
    }
    want = rows_arg(d, args, "limit");
    left = want;
    ais_find(a, text, tmp);
    rewind(tmp);
    text_open(q);
    while (left > 0 && fgets(line, sizeof line, tmp) != NULL) {
        jout(line);
        left--;
    }
    if (left == want)
        jout("no match");
    else if (left == 0)
        jout("(stopped at the limit; there may be more)\n");
    text_close(0);
    fclose(tmp);
}

static void tool_tags(ais *a, const jdoc *d, int args, const struct req *q)
{
    struct sink s;

    sink_init(&s, a, rows_arg(d, args, "limit"));
    text_open(q);
    ais_tags_page(a, 0, NULL, (int)s.left, on_tag, &s);
    if (s.rows == 0)
        jout("the index has no keys yet");
    text_close(0);
}

static void tool_timeline(ais *a, const jdoc *d, int args, const struct req *q)
{
    struct sink s;

    /* Named count here and limit on every other tool. Take either, rather than
     * handing back 200 rows to a model that guessed the wrong word. */
    sink_init(&s, a, rows_arg(d, args,
                              jget(d, args, "count") >= 0 ? "count" : "limit"));
    text_open(q);
    ais_timeline(a, 0, (int)s.left, NULL, NULL, on_tl, &s);
    rows_done(&s);
    text_close(0);
}

static void tool_save(ais *a, const jdoc *d, int args, const struct req *q, int allow_write)
{
    const char *value = jtext(d, jget(d, args, "value"));
    char keys[AIS_LINE_MAX];
    char *kv[AIS_KEYS_MAX];
    char msg[64];
    size_t len = 0;
    int nkeys, i;
    long id;

    if (!allow_write) {
        text_reply(q, "this index is read-only: restart the server as 'ais --mcp rw' to save", 1);
        return;
    }
    if (value == NULL || *value == '\0') {
        text_reply(q, "save needs a value", 1);
        return;
    }
    nkeys = keys_arg(d, jget(d, args, "keys"), kv, AIS_KEYS_MAX);
    if (nkeys == 0) {
        /* A record filed under nothing cannot be recalled by any key, ever.
         * Refusing here teaches at the moment the model is about to get it
         * wrong, which is worth more than the same sentence in every session. */
        text_reply(q, "save needs at least one key: ask which keys to file it under, "
                      "several separated by spaces, and offer the keys tags already lists", 1);
        return;
    }
    for (i = 0; i < nkeys; i++) {
        size_t n = strlen(kv[i]);
        if (len + n + 2 > sizeof keys)
            break;                                  /* keep what fits, as a put does */
        if (len > 0)
            keys[len++] = ' ';
        memcpy(keys + len, kv[i], n);
        len += n;
    }
    keys[len] = '\0';
    id = ais_put_value(a, keys, value);
    if (id < 0) {
        text_reply(q, "save failed", 1);
        return;
    }
    /* Name the keys back. The user said "file it under work ssh", the model may
     * have heard "work", and this line is the only place that difference shows
     * before the record is lost to the wrong word. */
    snprintf(msg, sizeof msg, "saved as record %ld under ", id);
    text_open(q);
    jout(msg);
    jout(keys);
    text_close(0);
}

/* ---- what the model is told ---------------------------------------------
 * Nobody types a tool call. People say "save this", "add it to my memory",
 * "what did I file under venice", and the model decides what that means from
 * this text and the descriptions below. So this is the real interface, and it
 * is written for a reader rather than for a parser. MCP hands it to the client
 * in the initialize reply.
 *
 * The one rule worth defending: the keys are the user's. A model that invents
 * a vocabulary on their behalf files their things under the average of
 * everyone's words, which is the thing this index exists not to be. When they
 * have not said what to file something under, ASK. */
static const char INSTRUCTIONS[] =
    "ais is this person's own associative index: things they filed under their own words, "
    "on their own disk, in plain text.\n"
    "\n"
    "When they say save, keep, remember, note, add to memory, add to the index or put it in "
    "ais, call save. If they named the keys ('save this under work ssh'), those words are the "
    "keys. If they did not, ASK which keys to file it under, say that several are separated by "
    "spaces, and offer what tags already lists. Do not invent a vocabulary for them, and do not "
    "file anything under a key they did not choose. Ask for as many keys as they want to give: "
    "each one is another way back to it.\n"
    "\n"
    "When they ask what they saved, or name words that sound like their own filing ('what do I "
    "have on venice', 'my ssh tunnel' is recall with the keys ssh tunnel), call recall first. "
    "Call find only when recall answers nothing: recall is an exact lookup on their keys, find "
    "is a substring search of the values. For 'what did I save lately' call timeline, which is "
    "also the only reply that shows a record's keys.\n"
    "\n"
    "Recall is exact, not fuzzy. Nothing back means nothing is filed under those keys, which is "
    "an answer rather than a failure: say so, and offer to search the values or list the keys, "
    "instead of guessing at near misses. Keys fold case, so there is no point retrying one in "
    "another case.";

/* Read-only is the default, and a model told to call a tool that is not in the
 * list will either claim it saved something or reach for a file of its own. */
static const char INSTRUCTIONS_RO[] =
    "\n\nThis server is read-only and has no save tool. If they ask to save something, say "
    "it was started without saving, and that 'ais --mcp rw' turns it on.";

/* ---- the tool list ------------------------------------------------------
 * Written out rather than generated: the descriptions are what a model reads
 * to decide whether to call, so they are prose, and prose belongs in one
 * readable place. */
static const char TOOLS[] =
"{\"name\":\"recall\",\"description\":\""
    "Recall what the user filed under their own keys. 'my ssh tunnel' is recall with the keys "
    "ssh tunnel; 'what do I have on venice' is recall with the key venice. "
    "Returns one 'id|value' per line. "
    "Keys are the user's vocabulary, not a fixed namespace: call tags first if you do not know them, "
    "and keys fold case. A wrong key returns nothing rather than something plausible. "
    "Two keys under match all return only records filed under BOTH, so try match any before "
    "reporting that they have nothing. "
    "A value starting with 'blobs/' is a document file, named relative to the index directory "
    "given above. A value starting with 'aisc:' is an encrypted secret and stays opaque here."
    "\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"keys\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"the keys to look under\"},"
    "\"match\":{\"type\":\"string\",\"enum\":[\"all\",\"any\"],\"description\":\"all = intersection (default), any = union\"},"
    "\"limit\":{\"type\":\"integer\",\"description\":\"maximum rows (default 200)\"}},"
    "\"required\":[\"keys\"]}},"
"{\"name\":\"find\",\"description\":\""
    "Search the stored values themselves for a substring, case-insensitive. "
    "Use when the user's key is unknown; recall by key is cheaper and exact. "
    "A document's stored value is its filename, so this does not read inside 'blobs/' files. "
    "Returns one 'id|value' per line."
    "\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"text\":{\"type\":\"string\",\"description\":\"substring to look for\"},"
    "\"limit\":{\"type\":\"integer\",\"description\":\"maximum rows (default 200)\"}},"
    "\"required\":[\"text\"]}},"
"{\"name\":\"tags\",\"description\":\""
    "List the keys this index actually uses, busiest first, as 'count|key' per line. "
    "The cheapest way to learn the user's vocabulary before recalling."
    "\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"limit\":{\"type\":\"integer\",\"description\":\"maximum keys (default 200)\"}}}},"
"{\"name\":\"timeline\",\"description\":\""
    "The most recently saved records, newest first, as 'id|timestamp|keys|value' per line. "
    "Answers 'what did I save lately', and is the only reply that shows a record's keys."
    "\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"count\":{\"type\":\"integer\",\"description\":\"how many records (default 200; "
    "limit means the same)\"}}}}";

static const char TOOL_SAVE[] =
",{\"name\":\"save\",\"description\":\""
    "File a value under keys, so it can be recalled by those keys later. This is what "
    "'save this', 'remember this', 'add it to my memory' and 'put it in ais' mean. "
    "Save a reference (a link, a path, a command, a short note), not a document body. "
    "If the user did not name the keys, ASK which keys to file it under, several separated "
    "by spaces, offering what tags already lists. Never invent a vocabulary for them."
    "\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"value\":{\"type\":\"string\",\"description\":\"what to store\"},"
    "\"keys\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},"
    "\"description\":\"the keys to file it under, the user's own words\"}},"
    "\"required\":[\"value\",\"keys\"]}}";

/* ---- dispatch ----------------------------------------------------------- */

/* Answer with the version the client asked for when it is one of the spec's
 * published revisions, since the tool surface here is the same across all
 * three; otherwise name ours and let the client decide. A version that merely
 * LOOKS like a date is not one: echoing it back agrees to a protocol nobody
 * has written. */
static void say_protocol(const char *want)
{
    static const char *const known[] = { "2024-11-05", "2025-03-26", "2025-06-18" };
    int i;

    for (i = 0; want != NULL && i < 3; i++)
        if (strcmp(want, known[i]) == 0) { jout(want); return; }
    jout(MCP_PROTOCOL);
}

static void handle(ais *a, int allow_write, char *line)
{
    const char *method, *tool, *ver;
    struct req q;
    jdoc d;
    char *p = line;
    int root, args;

    d.n = 0;
    root = jparse(&d, &p);
    if (root < 0) {
        reply_error(NULL, -32700, "parse error");
        return;
    }
    jskip(&p);
    if (*p != '\0') {                  /* a second value after the root object */
        reply_error(NULL, -32700, "parse error");
        return;
    }
    if (d.v[root].type != JOBJ) {
        /* A batch is a top-level array: well-formed JSON, so the fault is the
         * shape and not the parse. This transport carries one request object
         * per line and answers with id null, having read no id. */
        reply_error(NULL, -32600, "invalid request: one request object per line, no batch");
        return;
    }
    req_id(&d, root, &q);
    if (q.bad) {
        reply_error(NULL, -32600, "invalid request: id must be a string, a number or null");
        return;
    }
    ver = jtext(&d, jget(&d, root, "jsonrpc"));
    if (ver == NULL || strcmp(ver, "2.0") != 0) {
        reply_error(&q, -32600, "invalid request: jsonrpc must be 2.0");
        return;
    }
    method = jtext(&d, jget(&d, root, "method"));
    if (method == NULL) {
        if (q.has_id)
            reply_error(&q, -32600, "invalid request");
        return;
    }
    if (!q.has_id)
        return;              /* a notification (initialized, cancelled): nothing to answer */

    if (strcmp(method, "initialize") == 0) {
        reply_head(&q);
        fputs(",\"result\":{\"protocolVersion\":\"", stdout);
        say_protocol(jtext(&d, jget(&d, jget(&d, root, "params"), "protocolVersion")));
        fputs("\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"ais\",\"version\":\"",
              stdout);
        jout(ais_version());
        fputs("\"},\"instructions\":\"", stdout);
        jout(INSTRUCTIONS);
        if (!allow_write)
            jout(INSTRUCTIONS_RO);
        /* Which index, in words. A repo-local .ais/ and the personal ~/.ais are
         * the same protocol, and an agent that cannot tell them apart reports a
         * project's notes as the user's own memory. */
        jout("\n\nThis index is at ");
        jout(a->dir);
        jout(".");
        fputs("\"}", stdout);
        reply_end();
        return;
    }
    if (strcmp(method, "ping") == 0) {
        reply_head(&q);
        fputs(",\"result\":{}", stdout);
        reply_end();
        return;
    }
    if (strcmp(method, "tools/list") == 0) {
        reply_head(&q);
        fputs(",\"result\":{\"tools\":[", stdout);
        fputs(TOOLS, stdout);
        if (allow_write)
            fputs(TOOL_SAVE, stdout);
        fputs("]}", stdout);
        reply_end();
        return;
    }
    if (strcmp(method, "tools/call") != 0) {
        reply_error(&q, -32601, "method not found");
        return;
    }

    tool = jtext(&d, jget(&d, jget(&d, root, "params"), "name"));
    args = jget(&d, jget(&d, root, "params"), "arguments");
    if (tool == NULL)                              reply_error(&q, -32602, "no tool named");
    else if (strcmp(tool, "recall") == 0)          tool_recall(a, &d, args, &q);
    else if (strcmp(tool, "find") == 0)            tool_find(a, &d, args, &q);
    else if (strcmp(tool, "tags") == 0)            tool_tags(a, &d, args, &q);
    else if (strcmp(tool, "timeline") == 0)        tool_timeline(a, &d, args, &q);
    else if (strcmp(tool, "save") == 0)            tool_save(a, &d, args, &q, allow_write);
    else                                           reply_error(&q, -32602, "unknown tool");
}

int ais_mcp(ais *a, int allow_write)
{
    static char line[MCP_REQ_MAX];

    /* Read byte by byte rather than with fgets, which reports only a pointer:
     * a NUL inside the line would then end the C string early, and strlen's
     * answer would send the framing off by one message. Here the true length
     * is known, so a NUL is seen and the line is refused whole. */
    for (;;) {
        size_t n = 0;
        int c, over = 0, nul = 0;

        while ((c = getchar()) != EOF && c != '\n') {
            if (c == '\0')
                nul = 1;
            if (n + 1 < sizeof line)
                line[n++] = (char)c;
            else
                over = 1;
        }
        if (c == EOF && n == 0)
            return 0;                      /* clean close, or a trailing newline */
        line[n] = '\0';
        if (over || nul) {
            reply_error(NULL, -32600, over ? "request too large"
                                           : "request holds a NUL byte");
            continue;
        }
        if (n > 0)
            handle(a, allow_write, line);  /* a blank line between messages: skip */
    }
}
