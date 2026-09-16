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
#define MCP_PROTOCOL  "2025-06-18"  /* answered when the client names none     */

/* ---- reading JSON -------------------------------------------------------
 * Enough of RFC 8259 to read one JSON-RPC request. Strings are unescaped IN
 * PLACE inside the request buffer (unescaping never grows a string), and the
 * values live in one fixed node table, so a request costs no allocation and
 * bounded memory whatever the client sends. */

enum { JNONE = 0, JOBJ, JARR, JSTR, JNUM, JLIT };

typedef struct {
    char  type;
    char *key;        /* member name, when the parent is an object */
    char *str;        /* JSTR: the unescaped text */
    long  num;        /* JNUM: the integer part; JLIT: 1 for true */
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
    v->type = (char)type; v->key = NULL; v->str = NULL;
    v->num = 0; v->kid = -1; v->sib = -1;
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
                d->v[me].num = (i == 0);
                *p += n;
                return me;
            }
        }
        return -1;
    }
    default: {
        char *end;
        long n = strtol(*p, &end, 10);
        if (end == *p) return -1;
        me = jnew(d, JNUM);
        if (me < 0) return -1;
        d->v[me].num = n;
        /* A fraction or exponent is legal JSON and not something any field
         * here means; step over it so the rest of the message still reads. */
        while (*end == '.' || *end == 'e' || *end == 'E' || *end == '+' ||
               *end == '-' || (*end >= '0' && *end <= '9'))
            end++;
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
    return (i >= 0 && d->v[i].type == JNUM) ? d->v[i].num : dflt;
}

/* ---- writing JSON ------------------------------------------------------- */

/* S as the BODY of a JSON string: the escapes RFC 8259 demands and no others,
 * so UTF-8 passes through as the bytes the store holds. */
static void jout(const char *s)
{
    const unsigned char *u = (const unsigned char *)s;

    for (; *u != '\0'; u++) {
        switch (*u) {
        case '"':  fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout);  break;
        case '\r': fputs("\\r", stdout);  break;
        case '\t': fputs("\\t", stdout);  break;
        default:
            if (*u < 0x20)
                printf("\\u%04x", (unsigned)*u);
            else
                putchar((int)*u);
        }
    }
}

/* The request's id, echoed back verbatim: JSON-RPC lets it be a number or a
 * string, and a reply that changes its shape is a reply the client drops. */
struct req { int has_id, id_str; const char *id_s; long id_n; };

static void req_id(const jdoc *d, int root, struct req *q)
{
    int i = jget(d, root, "id");

    q->has_id = 0; q->id_str = 0; q->id_s = NULL; q->id_n = 0;
    if (i < 0)
        return;                                   /* a notification: no reply */
    if (d->v[i].type == JSTR)      { q->has_id = 1; q->id_str = 1; q->id_s = d->v[i].str; }
    else if (d->v[i].type == JNUM) { q->has_id = 1; q->id_n = d->v[i].num; }
}

static void reply_head(const struct req *q)
{
    fputs("{\"jsonrpc\":\"2.0\",\"id\":", stdout);
    if (q == NULL || !q->has_id)   fputs("null", stdout);
    else if (q->id_str)            { putchar('"'); jout(q->id_s); putchar('"'); }
    else                           printf("%ld", q->id_n);
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

static long rows_arg(const jdoc *d, int args, const char *name)
{
    long n = jint(d, jget(d, args, name), MCP_ROWS_DEF);

    return (n > 0) ? n : MCP_ROWS_DEF;
}

static void sink_init(struct sink *s, ais *a, long left)
{
    s->a = a; s->left = left; s->rows = 0;
}

/* An empty result is a fact, not a failure: say so in words, or a model reads
 * an empty block as a broken tool and tries again. */
static void rows_done(const struct sink *s)
{
    if (s->rows == 0)
        jout("no match");
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
    long left;
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
    left = rows_arg(d, args, "limit");
    ais_find(a, text, tmp);
    rewind(tmp);
    text_open(q);
    while (left > 0 && fgets(line, sizeof line, tmp) != NULL) {
        jout(line);
        left--;
    }
    if (left == rows_arg(d, args, "limit"))
        jout("no match");
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

    sink_init(&s, a, rows_arg(d, args, "count"));
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
    snprintf(msg, sizeof msg, "saved as record %ld", id);
    text_reply(q, msg, 0);
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
    "When they say save, keep, remember, note, add to memory or add to the index, call save. "
    "If they named the keys (\"save this under work ssh\"), use those words as the keys. "
    "If they did not, ASK which keys to file it under, and say that several are separated by "
    "spaces; suggest the closest keys the index already uses, from tags, and let them decide. "
    "Do not invent a vocabulary for them, and do not file something under a key they did not "
    "choose. A record can carry as many keys as they like, and more keys make it easier to "
    "reach later.\n"
    "\n"
    "When they ask what they saved, or name words that sound like their own filing "
    "(\"what do I have on venice\", \"my ssh tunnel\"), call recall first. Call find only when "
    "recall answers nothing: recall is an exact lookup on their keys, find is a substring "
    "search of the values. Call tags when you need to know what words this index actually "
    "uses.\n"
    "\n"
    "Recall is exact, not fuzzy. Nothing back means nothing is filed under those keys, which "
    "is an answer rather than a failure: say so, and offer to search the values or list the "
    "keys, instead of guessing at near misses.\n"
    "\n"
    "What belongs here is a reference: a link, a path, a command, a version, a short note, the "
    "thing they would otherwise hunt for twice. The index points at documents rather than "
    "holding them.";

/* ---- the tool list ------------------------------------------------------
 * Written out rather than generated: the descriptions are what a model reads
 * to decide whether to call, so they are prose, and prose belongs in one
 * readable place. */
static const char TOOLS[] =
"{\"name\":\"recall\",\"description\":\""
    "Recall what the user filed under their own keys: \"what did I save under X\", "
    "\"my ssh tunnel\", \"what do I have on venice\". Returns one 'id|value' per line. "
    "Keys are the user's vocabulary, not a fixed namespace: call tags first if you do not know them. "
    "A wrong key returns nothing rather than something plausible. "
    "A value starting with 'blobs/' is a document file inside the index; read it from disk. "
    "A value starting with 'aisc:' is an encrypted secret and stays opaque here."
    "\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"keys\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"the keys to look under\"},"
    "\"match\":{\"type\":\"string\",\"enum\":[\"all\",\"any\"],\"description\":\"all = intersection (default), any = union\"},"
    "\"limit\":{\"type\":\"integer\",\"description\":\"maximum rows (default 200)\"}},"
    "\"required\":[\"keys\"]}},"
"{\"name\":\"find\",\"description\":\""
    "Search the stored values themselves for a substring, case-insensitive. "
    "Use when the user's key is unknown; recall by key is cheaper and exact. "
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
    "The most recently saved records, newest first, as 'id|timestamp|keys|value' per line."
    "\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"count\":{\"type\":\"integer\",\"description\":\"how many records (default 200)\"}}}}";

static const char TOOL_SAVE[] =
",{\"name\":\"save\",\"description\":\""
    "File a value under keys, so it can be recalled by those keys later. This is what "
    "\"save this\", \"remember this\", \"add it to my memory\" and \"add to the index\" mean. "
    "Save a reference (a link, a path, a command, a short note), not a document body. "
    "If the user did not name the keys, ASK which keys to file it under, several separated "
    "by spaces, suggesting what the index already uses. Never invent a vocabulary for them."
    "\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"value\":{\"type\":\"string\",\"description\":\"what to store\"},"
    "\"keys\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"the keys to file it under\"}},"
    "\"required\":[\"value\"]}}";

/* ---- dispatch ----------------------------------------------------------- */

/* Answer with the version the client asked for when it looks like one of the
 * spec's dated revisions, since the tool surface here has been the same across
 * all of them; otherwise name ours and let the client decide. */
static void say_protocol(const char *want)
{
    int i;

    if (want != NULL && strlen(want) == 10 && want[4] == '-' && want[7] == '-') {
        for (i = 0; i < 10; i++)
            if (i != 4 && i != 7 && (want[i] < '0' || want[i] > '9'))
                break;
        if (i == 10) { jout(want); return; }
    }
    jout(MCP_PROTOCOL);
}

static void handle(ais *a, int allow_write, char *line)
{
    const char *method, *tool;
    struct req q;
    jdoc d;
    char *p = line;
    int root, args;

    d.n = 0;
    root = jparse(&d, &p);
    if (root < 0 || d.v[root].type != JOBJ) {
        reply_error(NULL, -32700, "parse error");
        return;
    }
    req_id(&d, root, &q);
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

    while (fgets(line, (int)sizeof line, stdin) != NULL) {
        size_t n = strlen(line);
        if (n == 0)
            continue;
        if (line[n - 1] != '\n' && !feof(stdin)) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF)
                ;                                  /* drop the rest of an oversized line */
            reply_error(NULL, -32600, "request too large");
            continue;
        }
        handle(a, allow_write, line);
    }
    return 0;
}
