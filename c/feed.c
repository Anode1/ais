/* feed.c -- bulk feeding values into the index: stdin lines (`-v -`),
 * interactive (`-i`), --import, and the CLI --doc streaming. One aspect, "file
 * many values under given keys", kept out of main.c so the CLI dispatcher stays
 * linear.
 *
 * Front-end code: it may die() on error, the same as main.c (the engine
 * modules only return codes). */
#define _POSIX_C_SOURCE 200809L      /* strtok_r */
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* getpid, unlink: the incoming-blob temp */

#include "compact.h"
#include "sync.h"      /* AIS_SYNC_PROTO -- an app bundle is these lines behind one byte */
#include "doc.h"
#include "feed.h"
#include "win.h"        /* mkdir() shim on native Windows */
#include "log.h"
#include "secret.h"
#include "store.h"        /* store_each_record -- merge export */

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

/* Strip a trailing newline/CR in place. */
static void chomp(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

/* Does this field start with a date? Used only to tell a merge-stream verb line
 * from a hand-written "keys|value" whose key happens to look like one. */
static int feed_looks_dated(const char *p)
{
    int i;
    /* The shape is required through the 'T': a merge line carries a full ISO-8601
     * stamp, "YYYY-MM-DDT..", while a hand-written "AI|2026-01-01 met Ann" has a
     * short uppercase field 1 and a date-like field 2 and must still import. */
    for (i = 0; i < 4; i++)
        if (p[i] < '0' || p[i] > '9') return 0;
    if (p[4] != '-') return 0;
    for (i = 5; i < 7; i++)
        if (p[i] < '0' || p[i] > '9') return 0;
    if (p[7] != '-') return 0;
    for (i = 8; i < 10; i++)
        if (p[i] < '0' || p[i] > '9') return 0;
    return p[10] == 'T';
}

/* Split "KEY... -v VALUE" in place. Returns 1 and points *KEYS and *VAL into LINE
 * on success, 0 if the line carries no -v/--value marker (an old |-separated
 * line; see feed_old_line). Hand-written and dumped lines share this one
 * grammar. -v takes the REST OF THE LINE verbatim -- no tokenising, quoting or
 * escaping, there being no shell here -- so a value may contain '|', '#', quotes
 * or backslashes, and the marker must come last. A key cannot be mistaken for
 * it: a key beginning '-' is refused on every path (that spelling is detach). */
static int parse_kv_line(char *line, char **keys, char **val)
{
    char *p = line, *m = NULL;
    size_t skip = 0;

    for (; *p != '\0'; p++) {
        int at_start = (p == line) || p[-1] == ' ' || p[-1] == '\t';
        if (!at_start)
            continue;
        if (strncmp(p, "-v ", 3) == 0)       { m = p; skip = 3; break; }
        if (strncmp(p, "--value ", 8) == 0)  { m = p; skip = 8; break; }
    }
    if (m == NULL)
        return 0;

    *val = m + skip;                          /* verbatim, to end of line */
    while (m > line && (m[-1] == ' ' || m[-1] == '\t'))
        m--;
    *m = '\0';                                /* keys end where the marker began */
    *keys = line;
    while (**keys == ' ' || **keys == '\t')
        (*keys)++;
    return 1;
}

/* An OLD pre-v2 line: "keys|value", or a --dump line "id|keys|value". Rewrites
 * LINE in place into the new shape and points *KEYS and *VAL at it.
 *
 * The leading field of a 3-field line becomes a KEY rather than being discarded
 * as an id: an id and a numeric tag are indistinguishable, so visible noise is
 * preferred to silent loss -- a real id becomes a junk key, a real tag like
 * "2024" survives. One case cannot be fixed: an old two-field line whose VALUE
 * contains '|' is indistinguishable from a three-field one and reads as one key
 * too many. That ambiguity is why the format changed. */
static void feed_old_line(char *line, char **keys, char **val, int *noisy)
{
    char *b1 = strchr(line, '|'), *b2;
    char *d;
    int numeric = 1;

    *noisy = 0;
    if (b1 == NULL) {                         /* no separator at all */
        *keys = line;
        *val = line + strlen(line);
        return;
    }
    b2 = strchr(b1 + 1, '|');
    if (b2 != NULL) {
        for (d = line; d < b1; d++)
            if (*d < '0' || *d > '9') { numeric = 0; break; }
        if (numeric && b1 > line) {
            *b1 = ' ';                        /* the id joins the keys field */
            *b2 = '\0';
            *keys = line;
            *val  = b2 + 1;
            *noisy = 1;                       /* a junk key was kept on purpose */
            return;
        }
    }
    *b1 = '\0';                               /* plain "keys|value" */
    *keys = line;
    *val  = b1 + 1;
}


void feed_interactive(ais *a, const char *base)
{
    const char *ttypath = getenv("AIS_TTY");   /* a file overrides the terminal */
    FILE *tty;
    char value[AIS_LINE_MAX];
    char typed[AIS_LINE_MAX];
    char keys[AIS_LINE_MAX];

#ifdef _WIN32
    tty = fopen(ttypath != NULL ? ttypath : "CONIN$", "r");   /* Windows console */
#else
    tty = fopen(ttypath != NULL ? ttypath : "/dev/tty", "r");
#endif
    if (tty == NULL)
        die("put -i: no terminal for keys (pipe values in, or set AIS_TTY=FILE)");

    /* Each stdin line is a value; its keys come from the terminal (Enter accepts
     * the base keys). Two separate streams: values from the pipe, keys from the
     * tty. */
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

/* Consume WANT raw bytes from IN into <index>/blobs/<basename of REL>. Refuses a
 * relative path with a separator so a crafted stream cannot write outside blobs/.
 * The bytes are consumed either way, so the parser stays in sync. 0/-1. */
static int feed_take_blob(ais *a, const char *rel, long want, FILE *in,
                          ais_blobmap *map)
{
    char tmp[AIS_PATH_MAX], dirp[AIS_PATH_MAX], outrel[AIS_PATH_MAX], buf[8192];
    FILE *out = NULL;
    long left = want;
    int rc;

    /* Stream the body to a temp file FIRST, then let doc.c decide where it goes.
     * The old code looked only at whether the NAME was free and, when it was not,
     * drained the arriving bytes into nothing and reported success -- so an
     * `ais --import` of a peer's document silently kept the local one of the same
     * name and pointed the peer's record at it. The bytes have to exist before
     * that question can be answered honestly.
     *
     * A dotfile: export_blobs_stream skips names starting with '.', so a temp
     * left by a crash never travels, and compaction sweeps it. */
    if (snprintf(dirp, sizeof dirp, "%s/blobs", a->dir) >= (int)sizeof dirp)
        return -1;
    mkdir(dirp, 0777);      /* win.h maps this to _mkdir */
    if (snprintf(tmp, sizeof tmp, "%s/blobs/.incoming-%ld.tmp", a->dir,
                 (long)getpid()) >= (int)sizeof tmp)
        return -1;
    out = fopen(tmp, "wb");
    while (left > 0) {                          /* consume the bytes either way, so
                                                 * the parser stays in sync */
        size_t chunk = (left > (long)sizeof buf) ? sizeof buf : (size_t)left;
        size_t n = fread(buf, 1, chunk, in);
        if (n == 0)
            break;
        if (out != NULL && fwrite(buf, 1, n, out) != n) { fclose(out); out = NULL; }
        left -= (long)n;
    }
    if (out == NULL || fclose(out) != 0) { unlink(tmp); return -1; }
    if (left != 0)          { unlink(tmp); return -1; }

    rc = ais_doc_blob_place(a->dir, rel, tmp, outrel, sizeof outrel);
    if (rc != 0) { unlink(tmp); return -1; }
    if (strcmp(outrel, rel) != 0 && map != NULL &&
        ais_blobmap_add(map, rel, outrel) != 0)
        return -1;
    return 0;
}

/* Apply the blob map to ONE record value, once: "blobs/X" and the encrypted
 * "aisc:@blobs/X" form both repoint. Returns V unchanged when nothing matched. */
static const char *feed_remap_value(const ais_blobmap *map, const char *v,
                                    char *buf, size_t bsz)
{
    const char *to;

    if (map == NULL || map->n == 0)
        return v;
    if (strncmp(v, "aisc:@", 6) == 0) {
        to = ais_blobmap_get(map, v + 6);
        if (to != NULL && snprintf(buf, bsz, "aisc:@%s", to) < (int)bsz)
            return buf;
        return v;
    }
    to = ais_blobmap_get(map, v);
    return (to != NULL) ? to : v;
}

void feed_import_from(ais *a, FILE *in)
{
    /* The map is per-STREAM, so this path owns one too: a document arriving on
     * stdin whose name is taken here lands under another name, and its record has
     * to be told, exactly as it is on the sync path. Passing NULL was how
     * `ais --import` left the arriving record pointing at the local body. */
    ais_blobmap map = { NULL, NULL, 0, 0 };
    feed_import_from_map(a, in, &map);
    ais_blobmap_free(&map);
}

/* A run of A| lines is SPOOLED and its values resolved in one store pass, where a
 * put per line scanned the store once per record: an import of n records cost
 * O(n^2) and a 20k-line one took 13 s on a desktop, a minute on a phone. Each
 * spooled line keeps only its value hash on the stack; the line itself waits in
 * a temp file as "attach_ts|ts|keys|value". A hash that repeats inside one batch
 * is left for the put to scan (-1): the first occurrence may append the record
 * the second names. A hash HIT is trusted only after the resolved id's store
 * line has been read back and compared whole: FNV-1a is not a security hash,
 * and a crafted collision would otherwise graft the arriving keys onto an
 * unrelated record and drop the arriving one. That read goes through "off"; an
 * index without a consistent "off" scans for its hits instead. A spool that
 * cannot be written ends the batching and the line is put directly, so a full
 * temp filesystem costs speed, never records. */
struct abatch { char hash[17]; long id; };

struct abatch_seek { struct abatch *b; int n, left; };

static int abatch_seek_cb(long id, const char *ts, const char *keys, const char *value, void *vp)
{
    struct abatch_seek *S = vp;
    char h[17];
    int i;

    (void)ts; (void)keys;
    content_hash(value, h);
    for (i = 0; i < S->n; i++) {
        if (S->b[i].id != 0 || strcmp(h, S->b[i].hash) != 0)
            continue;
        S->b[i].id = id;                   /* the FIRST line, where store_find_value stops */
        S->left--;
    }
    return (S->left == 0) ? -1 : 0;
}

struct same_value { const char *want; int same; };

static int same_value_cb(long id, const char *ts, const char *keys, const char *value, void *vp)
{
    struct same_value *V = vp;
    (void)id; (void)ts; (void)keys;
    V->same = (strcmp(value, V->want) == 0);
    return 0;
}

/* 1 if ID's first store line holds VALUE, 0 if it does not or cannot be read. */
static int abatch_verify(const ais *a, long id, const char *value)
{
    struct same_value V;
    long off;

    if (off_get(a, id, &off) != 1)
        return 0;
    V.want = value;
    V.same = 0;
    if (store_record_at(a, id, off, same_value_cb, &V) != 1)
        return 0;
    return V.same;
}

/* Resolve and apply the spooled run. BUF is a line-sized scratch the caller is not
 * using at this moment (the importer's remap buffer), so this frame adds no
 * AIS_LINE_MAX buffer of its own to the FFI stack budget (tests/stack). Returns
 * the number of records put; LOST counts spooled lines that could not be read
 * back, which the caller reports. Rewinds the spool for the next run. */
static long abatch_flush(ais *a, FILE *spool, struct abatch *b, int n,
                         char *buf, size_t bufsz, long *lost)
{
    struct abatch_seek S;
    long done = 0;
    int i, j, canverify;

    if (n == 0)
        return 0;
    S.b = b; S.n = n; S.left = n;
    for (i = 0; i < n; i++) {
        b[i].id = 0;
        for (j = 0; j < i; j++)
            if (strcmp(b[j].hash, b[i].hash) == 0) { b[i].id = -1; S.left--; break; }
    }
    if (fseek(spool, 0, SEEK_SET) != 0) {  /* the run is unreadable: say so, drop it */
        *lost += n;
        return 0;
    }
    if (store_wlock(a) != 0) { *lost += n; fseek(spool, 0, SEEK_SET); return 0; }
    if (store_load_next_id(a) != 0) { store_wunlock(a); *lost += n; fseek(spool, 0, SEEK_SET); return 0; }
    if (S.left > 0)
        store_each_record(a, abatch_seek_cb, &S);
    canverify = (off_consistent(a) > 0);
    for (i = 0; i < n; i++) {
        char *ats = buf, *ts, *k, *v;
        long found = b[i].id;
        if (store_read_line(buf, bufsz, spool) <= 0) {
            *lost += n - i;
            break;
        }
        chomp(buf);
        ts = strchr(ats, '|');
        k  = (ts != NULL) ? strchr(ts + 1, '|') : NULL;
        v  = (k  != NULL) ? strchr(k + 1, '|')  : NULL;
        if (ts == NULL || k == NULL || v == NULL) { (*lost)++; continue; }
        *ts++ = '\0';
        *k++ = '\0';
        *v++ = '\0';
        if (found > 0 && !(canverify && abatch_verify(a, found, v)))
            found = -1;                    /* a hit the store line does not confirm: scan */
        if (ais_put_at_k_resolved(a, k, v, ts[0] ? ts : NULL, ats[0] ? ats : NULL, found) >= 0)
            done++;
    }
    store_wunlock(a);
    fseek(spool, 0, SEEK_SET);
    return done;
}

/* The verb tests the flush guard relies on: a line that only STARTS like one is
 * a legacy "keys|value" record whose single key is A or C, and must flush too. */
static int is_add_line(const char *l)
{
    const char *p;
    if (l[0] != 'A' || l[1] != '|') return 0;
    p = strchr(l + 2, '|');
    return p != NULL && strchr(p + 1, '|') != NULL;
}
static int is_ctime_line(const char *l)
{
    return l[0] == 'C' && l[1] == '|' && strchr(l + 2, '|') != NULL;
}

void feed_import_from_map(ais *a, FILE *in, ais_blobmap *map)
{
    char line[AIS_LINE_MAX];
    char vbuf[AIS_LINE_MAX];              /* a remapped value, when one is remapped */
    ais_del_fact pend[AIS_MERGE_BATCH];   /* D| facts awaiting one shared store pass */
    int  npend = 0;
    struct abatch padd[AIS_MERGE_BATCH];  /* A| lines likewise (abatch_flush) */
    int  nadd = 0;
    long lost = 0;                        /* spooled lines that could not be replayed */
    FILE *spool = tmpfile();              /* NULL: no batching, a put per line */
    int  hasedits = (edits_active(a) > 0);   /* one stat, not a file open per line */
    struct edits_mem edmem;                  /* the edit log, loaded on first need */
    int  edstate = 0;                        /* 0 not loaded, 1 loaded, -1 failed */
    ais_att_fact patt[AIS_ATT_BATCH];     /* T| facts likewise (ais_merge_attach_many) */
    int  natt = 0;
    long n = 0, skipped = 0;
    int  warned_old = 0;      /* say "this is the old format" once, not per line */
    char cts[AIS_TS_MAX], chash[17];      /* one pending C| (a raised A|'s true time) */

    cts[0] = '\0';
    chash[0] = '\0';

    /* A bundle written by the app or the web GUI is a version byte followed by
     * exactly these lines. Eat the byte: glued to the first line it matches no
     * verb and falls through to the plain "keys|value" reader, storing the record
     * under a junk key with its timestamp swallowed into the value. */
    {
        int c0 = getc(in);
        if (c0 != EOF && c0 != AIS_SYNC_PROTO)
            ungetc(c0, in);
    }

    /* A line is one of: a merge-stream "A|ts|keys|value" (add) or "D|ts|hash" (delete),
     * or a plain "keys|value" (legacy dump / hand-edit -> an add with no ts). Keys never
     * contain '|' (key_encode maps it to '_'). Blank/#-comment lines are skipped, so the
     * plain form stays hand-editable; a malformed A|/D| line falls through to plain. */
    for (;;) {
        char *keys, *val;
        int rl = store_read_line(line, sizeof(line), in);

        if (rl == 0)
            break;
        /* Refuse the whole over-long line, once: taken as two, its tail would
         * parse as a record the input never contained. */
        if (rl < 0) {
            fprintf(stderr, "import: line longer than %d bytes, skipped whole\n",
                    AIS_LINE_MAX - 1);
            skipped++;
            continue;
        }

        chomp(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;

        /* D|ts|hash -- BUFFERED: resolving one scans the store for the value it
         * names, so per line an import costs O(deletes x records) where a whole
         * batch resolves in one pass (ais_merge_del_many). Tested before every
         * other verb, since those all write or read a record and must see the tomb
         * the pending deletes write. The batch therefore spans a RUN of D| lines --
         * all of them in practice, an export emitting every add before the first
         * delete. A malformed D| falls through to the flush below, then the plain
         * reader. */
        if (line[0] == 'D' && line[1] == '|' && strchr(line + 2, '|') != NULL) {
            char *ts = line + 2, *h = strchr(ts, '|');
            /* Keep STREAM order between the two buffers: this branch is tested
             * before the T| flush below, so a T| run followed by a D| run would
             * otherwise apply D| first, making the outcome depend on where the
             * buffers happen to break. */
            if (natt > 0) {
                ais_merge_attach_many(a, patt, natt);
                natt = 0;
            }
            if (nadd > 0) {                            /* a delete may name a spooled add */
                n += abatch_flush(a, spool, padd, nadd, vbuf, sizeof vbuf, &lost);
                nadd = 0;
            }
            *h++ = '\0';
            /* Bounded precision, not just a bounded buffer: a malformed line can
             * carry a field of any length, and it is truncated here. */
            snprintf(pend[npend].hash, sizeof pend[npend].hash, "%.*s",
                     (int)(sizeof pend[npend].hash - 1), h);
            snprintf(pend[npend].ts, sizeof pend[npend].ts, "%.*s",
                     (int)(sizeof pend[npend].ts - 1), ts);
            if (++npend == AIS_MERGE_BATCH) {          /* buffer full: resolve them */
                ais_merge_del_many(a, pend, npend);
                npend = 0;
            }
            continue;
        }
        if (npend > 0) {                               /* the run ended here */
            ais_merge_del_many(a, pend, npend);
            npend = 0;
        }
        /* Every verb but A| and C| reads or writes a record the spooled adds
         * may create, so the run of adds ends here and is applied first. C|
         * names the A| after it and is spent when that line is spooled. */
        if (nadd > 0 && !is_add_line(line) && !is_ctime_line(line)) {
            n += abatch_flush(a, spool, padd, nadd, vbuf, sizeof vbuf, &lost);
            nadd = 0;
        }

        /* E|ts|hash|value -- an edit made elsewhere, applied in place to the
         * record still holding the old value (ais_merge_edit). Every buffer is
         * flushed above, so the record it names is as the stream left it. */
        if (line[0] == 'E' && line[1] == '|') {
            char *ts = line + 2, *h, *v;
            h = strchr(ts, '|');
            v = (h != NULL) ? strchr(h + 1, '|') : NULL;
            if (h != NULL && v != NULL) {
                *h++ = '\0';
                *v++ = '\0';
                v = (char *)feed_remap_value(map, v, vbuf, sizeof vbuf);
                ais_merge_edit(a, h, v, ts);
                hasedits = 1;   /* sampled before the loop; this line may be the first fact */
                if (edstate == 1) { edits_mem_free(&edmem); }
                edstate = 0;    /* the log grew: reload before the next translation */
                continue;
            }
        }

        /* T|ts|hash|key -- BUFFERED for the same reason as D|, and tested here so
         * the pending deletes above are already applied (an attach asks whether
         * the record is still live). An export emits every attach in one run, from
         * one katt pass: in practice a single batch per bundle. */
        if (line[0] == 'T' && line[1] == '|') {
            char *ts = line + 2, *h, *k;
            h = strchr(ts, '|');
            k = (h != NULL) ? strchr(h + 1, '|') : NULL;
            if (h != NULL && k != NULL) {
                *h++ = '\0';                           /* ts | */
                *k++ = '\0';                           /* hash | key */
                snprintf(patt[natt].hash, sizeof patt[natt].hash, "%.*s",
                         (int)(sizeof patt[natt].hash - 1), h);
                snprintf(patt[natt].key, sizeof patt[natt].key, "%.*s",
                         (int)(sizeof patt[natt].key - 1), k);
                snprintf(patt[natt].ts, sizeof patt[natt].ts, "%.*s",
                         (int)(sizeof patt[natt].ts - 1), ts);
                if (++natt == AIS_ATT_BATCH) {         /* buffer full: resolve them */
                    ais_merge_attach_many(a, patt, natt);
                    natt = 0;
                }
                continue;
            }
        }
        if (natt > 0) {                                /* the run ended here */
            ais_merge_attach_many(a, patt, natt);
            natt = 0;
        }

        if (line[0] == 'C' && line[1] == '|') {        /* C|true ts|hash */
            /* The true time of the A| line that follows, which exports at a time
             * raised to beat a peer's tombstone. ONE slot: it names the very next
             * record, the exporter emitting it immediately before. */
            char *ts = line + 2, *h = strchr(ts, '|');
            if (h != NULL) {
                *h++ = '\0';
                snprintf(cts, sizeof cts, "%.*s", (int)(sizeof cts - 1), ts);
                snprintf(chash, sizeof chash, "%.*s", (int)(sizeof chash - 1), h);
                continue;
            }
        }
        if (line[0] == 'A' && line[1] == '|') {        /* A|ts|keys|value */
            char *ts = line + 2, *k, *v;
            k = strchr(ts, '|');
            v = (k != NULL) ? strchr(k + 1, '|') : NULL;
            if (k != NULL && v != NULL) {
                const char *ats;
                *k++ = '\0';
                *v++ = '\0';
                /* A timestamp the store reader would not take as one lands in
                 * the ts column anyway, where every reader then shifts the
                 * fields: the key index sees one record and --compact rebuilds
                 * another. Refuse the line, as an over-long one is refused. */
                if (ts[0] != '\0' && !store_looks_like_ts(ts)) {
                    fprintf(stderr, "import: not a timestamp, line skipped: %.40s\n", ts);
                    skipped++;
                    continue;
                }
                ats = ts[0] ? ts : NULL;
                v = (char *)feed_remap_value(map, v, vbuf, sizeof vbuf);
                /* A copy stamped before an edit this index knows about is the
                 * OLD text: read it as what it became, or a peer's stale bundle
                 * (or an old backup) recreates the text that was edited away as
                 * a second record. A record saved AFTER the edit is a new note. */
                if (hasedits && v != vbuf) {
                    char h[17];
                    content_hash(v, h);
                    if (edstate == 0)
                        edstate = (edits_mem_load(a, &edmem) == 0) ? 1 : -1;
                    if (edstate == 1 &&
                        edits_mem_lookup(&edmem, h, ts, vbuf, sizeof vbuf) == 1)
                        v = vbuf;
                }
                if (cts[0] != '\0') {          /* a C| named this record's true time */
                    char h[17];
                    content_hash(v, h);
                    if (strcmp(h, chash) == 0)
                        ats = cts;
                }
                /* The slot is spent by this A| either way, AFTER its time has been
                 * read: cleared first, the attach ran at an empty timestamp, which
                 * loses to every detach, rather than at the true one. */
                /* Spool it, unless the line would not read back whole (a remapped
                 * value can grow) or the spool fails: then the batch so far is
                 * applied, the spool is closed for good, and this line and every
                 * later one take the direct path. glibc keeps accepting writes
                 * on a stream in error, so the check is ferror after a flush. */
                {
                int spool_dead = 0;
                char *keep = NULL;
                if (spool != NULL &&
                    strlen(ts) + strlen(k) + strlen(v) + AIS_TS_MAX + 4 < AIS_LINE_MAX) {
                    fprintf(spool, "%s|%s|%s|%s\n", ats ? ats : "", ts, k, v);
                    if (fflush(spool) == 0 && !ferror(spool)) {
                        content_hash(v, padd[nadd].hash);
                        cts[0] = '\0';
                        if (++nadd == AIS_MERGE_BATCH) {
                            n += abatch_flush(a, spool, padd, nadd, vbuf, sizeof vbuf, &lost);
                            nadd = 0;
                        }
                        continue;
                    }
                    spool_dead = 1;        /* flushed below first: it still reads back */
                }
                /* The direct path, in stream order: the batch AHEAD of this line
                 * is applied first, or a long line's record kept the later
                 * timestamp, the field a peer's tombstone is compared against.
                 * The flush borrows vbuf, where v may still point (a remapped or
                 * edit-translated value): moved to the heap for the flush, and
                 * the line counted lost when it cannot be. */
                if (nadd > 0) {
                    if (v == vbuf) {
                        keep = strdup(v);
                        v = keep;          /* NULL on OOM: refused below, not stored clobbered */
                    }
                    n += abatch_flush(a, spool, padd, nadd, vbuf, sizeof vbuf, &lost);
                    nadd = 0;
                }
                if (spool_dead) {
                    fclose(spool);
                    spool = NULL;
                    fprintf(stderr, "import: the temp file failed, the rest goes one record at a time\n");
                }
                if (v == NULL)
                    lost++;
                else if (ais_put_at_k(a, k, v, ts[0] ? ts : NULL, ats) >= 0)
                    n++;
                free(keep);
                cts[0] = '\0';
                continue;
                }
            }                                          /* else: legacy keys "A" -> below */
        }
        if (line[0] == 'K' && line[1] == '|') {        /* K|ts|hash|key (detach a tag) */
            char *ts = line + 2, *h, *k;
            h = strchr(ts, '|');
            k = (h != NULL) ? strchr(h + 1, '|') : NULL;
            if (h != NULL && k != NULL) {
                *h++ = '\0';                           /* ts | */
                *k++ = '\0';                           /* hash | key */
                ais_merge_detach(a, h, k, ts);
                continue;
            }
        }

        if (line[0] == 'B' && line[1] == '|') {        /* B|blobs/<name>|<size> + raw bytes */
            /* Write the document body back before the record that points at it.
             * The header is followed by exactly <size> raw bytes, which are NOT
             * lines: read them by length, or the parser interprets a document's
             * contents as records. */
            char *rel = line + 2, *szp;
            long want;
            szp = strchr(rel, '|');
            if (szp != NULL) {
                *szp++ = '\0';
                want = atol(szp);
                if (want >= 0 && feed_take_blob(a, rel, want, in, map) != 0)
                    fprintf(stderr, "import: could not store %s\n", rel);
                continue;
            }
        }

        if (line[0] == 'M' && line[1] == '|') {        /* M|ts|hash|value */
            char *ts = line + 2, *h, *v;
            h = strchr(ts, '|');
            v = (h != NULL) ? strchr(h + 1, '|') : NULL;
            if (h != NULL && v != NULL) {
                *h++ = '\0';
                *v++ = '\0';
                v = (char *)feed_remap_value(map, v, vbuf, sizeof vbuf);
                /* A stale copy of a link edited away here comes back as what it
                 * became, as an A| does. A stale HASH (the record's first value
                 * was the one edited) names nothing and the link waits for the
                 * peer's next bundle, which carries the current one. */
                if (hasedits && v != vbuf) {
                    char vh[17];
                    content_hash(v, vh);
                    if (edstate == 0)
                        edstate = (edits_mem_load(a, &edmem) == 0) ? 1 : -1;
                    if (edstate == 1 &&
                        edits_mem_lookup(&edmem, vh, ts, vbuf, sizeof vbuf) == 1)
                        v = vbuf;
                }
                ais_merge_addval(a, h, v, ts);
                continue;
            }
        }

        /* A verb we do not know is REFUSED, never passed to the plain "keys|value"
         * reader, which would turn a future verb (or a binary bundle's header)
         * into a junk record under a fabricated key and report success. Refusing
         * is what makes the stream extensible: an older peer skips a new verb. */
        {
            /* The verb shape: 1 to 3 uppercase/digit characters before the first
             * '|', followed by a timestamp. One letter is not enough -- "D2|<ts>|
             * <hash>" would become a record keyed "D2". The timestamp test keeps a
             * legitimate tag safe: hand-written "TODO|buy milk" has no date in
             * field 2 and still imports as a record. */
            size_t vl = strcspn(line, "|");
            if (vl >= 1 && vl <= 3 && line[vl] == '|') {
                size_t c;
                int isverb = 1;
                for (c = 0; c < vl; c++)
                    if (!((line[c] >= 'A' && line[c] <= 'Z') ||
                          (line[c] >= '0' && line[c] <= '9')))
                        isverb = 0;
                if (isverb && !(line[0] >= '0' && line[0] <= '9') &&
                    feed_looks_dated(line + vl + 1)) {
                    fprintf(stderr, "import: unknown '%.*s|' record, skipped "
                                    "(from a newer ais?): %.60s\n",
                            (int)vl, line, line);
                    skipped++;
                    continue;
                }
            }
        }
        /* A bundle is not a stream: it has a binary header and length-prefixed
         * blobs, so reading it line-by-line invents records. Name the mistake. */
        if (n == 0 && strncmp(line, "AISB", 4) == 0) {
            fprintf(stderr, "import: this is a sync bundle, not an export stream.\n"
                            "        use: ais --sync-folder DIR\n");
            if (spool != NULL)
                fclose(spool);
            if (edstate == 1)
                edits_mem_free(&edmem);
            return;
        }

        /* Either the current grammar, "KEY... -v VALUE", or a pre-v2 '|'-separated
         * line. The -v marker decides; a key cannot fake it, a key beginning '-'
         * being refused everywhere. */
        if (!parse_kv_line(line, &keys, &val)) {
            int noisy = 0;
            if (strchr(line, '|') == NULL) {
                fprintf(stderr, "import: no -v, skipped: %s\n", line);
                skipped++;
                continue;
            }
            feed_old_line(line, &keys, &val, &noisy);
            if (!warned_old) {
                fprintf(stderr,
                    "import: reading pre-v2 '|' lines. The current format is\n"
                    "        KEY... -v VALUE  (see `ais --help`).\n");
                warned_old = 1;
            }
            if (noisy)
                fprintf(stderr, "import: kept leading field as a key (was it an id?): %s\n",
                        keys);
        }
        /* A keyless record is legal (--untag leaves them) and "-v VALUE" says
         * keyless outright, so there is nothing to disambiguate. */
        if (ais_put(a, keys, val) < 0) {       /* shared with the sync merge: skip, don't abort */
            fprintf(stderr, "import: skipped (put failed): %s\n", val);
            skipped++;
            continue;
        }
        n++;
    }
    if (nadd > 0)                                      /* end of stream: adds first */
        n += abatch_flush(a, spool, padd, nadd, vbuf, sizeof vbuf, &lost);
    if (npend > 0)                                     /* then the last deletes */
        ais_merge_del_many(a, pend, npend);
    if (natt > 0)                                      /* likewise the pending attaches */
        ais_merge_attach_many(a, patt, natt);
    if (spool != NULL)
        fclose(spool);
    if (edstate == 1)
        edits_mem_free(&edmem);
    if (lost > 0) {
        fprintf(stderr, "import: %ld records lost to a failed temp file or allocation\n", lost);
        skipped += lost;
    }
    if (skipped > 0)
        fprintf(stderr, "imported %ld, skipped %ld\n", n, skipped);
    else
        fprintf(stderr, "imported %ld\n", n);
}

void feed_import(ais *a) { feed_import_from(a, stdin); }

/* The merge/export stream: A|ts|keys|value for every live record, then D|ts|hash
 * for every content-addressed tombstone. Adds precede deletes so a delete in the
 * same stream applies after its add. */
struct exp_ctx { ais *a; FILE *out; int hasktomb; int hassts; };
/* Effective keys: drop any locally-detached (ktomb'd) key, so the export never carries
 * a removed tag that a peer would re-attach. Only run when ktomb has entries. */
static void exp_eff_keys(ais *a, long id, const char *keys, char *out, size_t outsz)
{
    char buf[AIS_LINE_MAX];
    char *tok, *save;
    size_t used = 0;
    int n = snprintf(buf, sizeof buf, "%s", keys);
    out[0] = '\0';
    if (n < 0 || (size_t)n >= sizeof buf) { snprintf(out, outsz, "%s", keys); return; }
    for (tok = strtok_r(buf, " \t", &save); tok != NULL; tok = strtok_r(NULL, " \t", &save)) {
        if (ktomb_contains(a, id, tok) == 1)
            continue;                    /* detached: not part of the effective record */
        n = snprintf(out + used, outsz - used, "%s%s", used ? " " : "", tok);
        if (n < 0 || used + (size_t)n >= outsz)
            break;
        used += (size_t)n;
    }
}
/* Grab a record's FIRST value, to tell an A| line from an M| one. */
struct first_val { char v[AIS_LINE_MAX]; int got; };
static int first_val_cb(long id, const char *value, void *ctx)
{
    struct first_val *F = ctx;
    (void)id;
    snprintf(F->v, sizeof F->v, "%s", value);
    F->got = 1;
    return 1;                                  /* first line only */
}

static int exp_live(long id, const char *ts, const char *keys, const char *value, void *vp)
{
    struct exp_ctx *E = vp;
    const char *k = keys;
    const char *tstrue = ts;              /* the line's own time, never raised */
    char eff[AIS_LINE_MAX];
    char ets[AIS_TS_MAX];

    if (tomb_contains(E->a, id) != 0)
        return 0;
    /* A record that has survived a peer's delete must reach that peer NEWER than
     * its tombstone, or the peer keeps the delete and resends it every round. The
     * raise lives here, not in the store line (compact.h). Gated, like the ktomb
     * work below: most indexes have no survivals, and the export must not open a
     * file per record to discover that. */
    if (E->hassts) {
        sts_effective(E->a, id, ts, ets, sizeof ets);
        ts = ets;
    }
    if (E->hasktomb) {
        exp_eff_keys(E->a, id, keys, eff, sizeof eff);
        k = eff;
    }

    /* A record may hold several values, each its own store line. The first value
     * carries the record (A|); the rest say "attach me to the record whose first
     * value hashes to this" (M|). Emitting every line as A| would make the
     * importer create a SEPARATE RECORD per value, splitting one 3-link record
     * into three. An older peer skips M| and gets one link: lossy, not wrong. */
    if (multi_contains(E->a, id) == 1) {
        struct first_val F;
        F.got = 0;
        F.v[0] = '\0';
        ais_record(E->a, id, first_val_cb, &F);
        if (F.got && strcmp(F.v, value) != 0) {
            char h[17];
            content_hash(F.v, h);
            /* The line's OWN time, never the survival raise: the raise rides the
             * A| line, and a raised M| reads as newer than an edit it predates. */
            fprintf(E->out, "M|%s|%s|%s\n", tstrue, h, value);
            return 0;
        }
    }
    /* The raise must decide one thing only: this record beats that peer's
     * tombstone. The importer also hands the A| timestamp to the key-attach
     * comparison, where a raised line outranks every peer's key tombstone and
     * re-attaches tags they deliberately removed. C| carries the line's TRUE time
     * beside it, keyed to this value's hash, for the importer to answer key
     * questions with; a peer predating the verb skips the line and still
     * converges. Keyed to the FIRST value, the rest going out as M|. */
    if (E->hassts && strcmp(ts, tstrue) != 0) {
        char h[17];
        content_hash(value, h);
        fprintf(E->out, "C|%s|%s\n", tstrue, h);
    }
    fprintf(E->out, "A|%s|%s|%s\n", ts, k, value);
    return 0;
}
/* E|ts|hash|value: an in-place edit, FIRST in the stream. After the A| line for
 * the new value the importer would have made it a second record, since it
 * finds no record holding it; before, the peer's record is edited in place and
 * the A| then finds it. */
static int exp_edit(const char *ts, const char *hash, const char *value, void *vp)
{
    struct exp_ctx *E = vp;
    fprintf(E->out, "E|%s|%s|%s\n", ts, hash, value);
    return 0;
}
static int exp_dead(long id, const char *ts, const char *hash, void *vp)
{
    struct exp_ctx *E = vp;
    (void)id;
    if (hash[0] != '\0')              /* skip legacy/compacted entries with no hash */
        fprintf(E->out, "D|%s|%s\n", ts, hash);
    return 0;
}
static int exp_kdead(long id, const char *ts, const char *hash, const char *key, void *vp)
{
    struct exp_ctx *E = vp;
    (void)id;
    if (hash[0] != '\0' && key[0] != '\0')       /* only content-addressed detaches carry */
        fprintf(E->out, "K|%s|%s|%s\n", ts, hash, key);
    return 0;
}
static int exp_kborn(long id, const char *ts, const char *hash, const char *key, void *vp)
{
    struct exp_ctx *E = vp;
    (void)id;
    if (hash[0] != '\0' && key[0] != '\0')
        fprintf(E->out, "T|%s|%s|%s\n", ts, hash, key);
    return 0;
}
/* Emit every file under <dir>/blobs/ as a "B|blobs/<name>|<size>\n" header plus
 * its raw bytes. A --doc record stores only the PATH to its body, so without this
 * an export carries a pointer to a file the other side never receives. sync.c
 * emits the same section for bundles; the two should be unified once sync.c's
 * copy can be reached from here. A missing blobs/ dir is not an error.
 *
 * CAP is a running ceiling on the bytes written, or 0 for none, checked BEFORE
 * each document is streamed: a bundle assembles into memory (open_memstream,
 * sync.c), so testing the total at the end allocates a multi-gigabyte blobs/
 * before rejecting it -- an OOM kill on a phone. Returns 0, or -1 once the cap
 * would be passed (the caller abandons the whole bundle; a partial one must
 * never be sent). */
/* The blob names LIVE records point at. Gathering them costs one store pass and
 * saves shipping files nothing refers to: an orphan (a document whose record was
 * edited away by an older build, say) used to ride EVERY export to EVERY peer,
 * for ever, because the exporter streamed the whole directory. */
struct blobrefs { char (*name)[AIS_PATH_MAX]; int n, cap; };

static int blobrefs_add(struct blobrefs *r, const char *base)
{
    int i;
    for (i = 0; i < r->n; i++)
        if (strcmp(r->name[i], base) == 0)
            return 0;
    if (r->n == r->cap) {
        int cap = (r->cap == 0) ? 16 : r->cap * 2;
        void *p = realloc(r->name, (size_t)cap * sizeof *r->name);
        if (p == NULL)
            return -1;
        r->name = p;
        r->cap = cap;
    }
    snprintf(r->name[r->n], AIS_PATH_MAX, "%s", base);
    r->n++;
    return 0;
}

static int blobrefs_has(const struct blobrefs *r, const char *base)
{
    int i;
    for (i = 0; i < r->n; i++)
        if (strcmp(r->name[i], base) == 0)
            return 1;
    return 0;
}

/* One store line: note the blob its value names, plain or encrypted. */
static int blobrefs_line(long id, const char *ts, const char *keys,
                         const char *value, void *vp)
{
    struct blobrefs *r = vp;
    const char *v = value;

    (void)id; (void)ts; (void)keys;
    if (strncmp(v, "aisc:@", 6) == 0)
        v += 6;
    if (strncmp(v, "blobs/", 6) != 0)
        return 0;
    return (blobrefs_add(r, v + 6) != 0) ? -1 : 0;
}

static int export_blobs_stream(FILE *out, const char *dir, size_t cap,
                               const struct blobrefs *refs)
{
    long skipped = 0, orphans = 0;
    char blobsdir[AIS_PATH_MAX], path[AIS_PATH_MAX], buf[8192];
    DIR *d;
    struct dirent *de;
    size_t total = 0;
    int rc = 0;

    if (snprintf(blobsdir, sizeof blobsdir, "%s/blobs", dir) >= (int)sizeof blobsdir)
        return 0;
    d = opendir(blobsdir);
    if (d == NULL)
        return 0;
    while ((de = readdir(d)) != NULL) {
        FILE *bf;
        long sz;
        size_t n;
        if (de->d_name[0] == '.')
            continue;
        if (refs != NULL && !blobrefs_has(refs, de->d_name)) {
            orphans++;               /* nothing here points at it: it is not ours to send */
            continue;
        }
        if (snprintf(path, sizeof path, "%s/%s", blobsdir, de->d_name) >= (int)sizeof path)
            continue;
        bf = fopen(path, "rb");
        if (bf == NULL)
            continue;
        if (fseek(bf, 0, SEEK_END) != 0 || (sz = ftell(bf)) < 0 ||
            fseek(bf, 0, SEEK_SET) != 0) { fclose(bf); continue; }
        if (cap > 0 && (total > cap || (size_t)sz > cap - total)) {
            /* Skip THIS document and keep going. Abandoning the bundle meant one
             * oversized document stopped every record on the device from ever
             * syncing, and sync is what stands in for a backup here, so the cost
             * of the old behaviour was every note saved afterwards living on one
             * device only. The record still travels; its body arrives when the
             * peer is reachable by other means. */
            fclose(bf);
            fprintf(stderr, "sync: %s is %ld bytes, past the %lu-byte transfer cap: "
                            "its text stays on this device\n",
                    de->d_name, sz, (unsigned long)cap);
            skipped++;
            continue;
        }
        total += (size_t)sz;
        fprintf(out, "B|blobs/%s|%ld\n", de->d_name, sz);
        while ((n = fread(buf, 1, sizeof buf, bf)) > 0)
            fwrite(buf, 1, n, out);
        fclose(bf);
    }
    closedir(d);
    if (skipped > 0)
        fprintf(stderr, "sync: %ld document%s did not travel; everything else did\n",
                skipped, skipped == 1 ? "" : "s");
    if (orphans > 0)
        debug("export: %ld blob file(s) no record points at, left here", orphans);
    return rc;
}

void feed_export(ais *a, FILE *out)
{
    /* `--export` goes to a pipe or a file the user chose: no memory ceiling to
     * respect, only the in-memory bundle needs one. */
    feed_export_capped(a, out, 0);
}

int feed_export_capped(ais *a, FILE *out, size_t blob_cap)
{
    struct exp_ctx E;
    E.a = a;
    E.out = out;
    E.hasktomb = (ktomb_active(a) > 0);
    E.hassts   = (sts_active(a) > 0);   /* one stat, not a seek per record */
    {   /* which bodies are still spoken for, then those bodies, then the records
         * (bodies first: a record may point at one) */
        struct blobrefs refs = { NULL, 0, 0 };
        int brc;
        store_each_record(a, blobrefs_line, &refs);
        brc = export_blobs_stream(out, a->dir, blob_cap, &refs);
        free(refs.name);
        if (brc != 0)
            return -1;
    }
    edits_each(a, exp_edit, &E);
    store_each_record(a, exp_live, &E);
    tomb_each(a, exp_dead, &E);
    ktomb_each(a, exp_kdead, &E);            /* key-detaches propagate as K| lines (I1) */
    /* and key-attaches as T|, AFTER the A| lines: the record must exist on the far
     * side before a key can be attached to it. Only keys put on an already
     * existing record are in katt, so a never-re-tagged index emits none. */
    katt_each(a, exp_kborn, &E);
    return 0;
}

/* CLI --doc: stream stdin (any size, bounded memory) into a blob, then put its
 * path. The blob naming and the path-put are shared with the GUIs via doc.c;
 * only the streaming-from-stdin part is CLI-specific, so it stays here. */
void feed_doc(ais *a, const char *keys)
{
    char blobpath[AIS_PATH_MAX], relval[AIS_PATH_MAX];
    FILE *bf;
    char buf[8192];
    size_t n;
    long got;

    if (ais_doc_blobname(a, relval, sizeof(relval), blobpath, sizeof(blobpath)) != 0)
        die("doc: cannot prepare blob path under '%s'", a->dir);

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

/* --import with a per-record gate: show each stdin "keys|value" record and read a
 * [y/N] answer from the terminal (/dev/tty, or $AIS_TTY for scripting/tests), so
 * records and answers stay on separate streams. Only y/Y takes the record. */
void feed_import_interactive(ais *a)
{
    const char *ttypath = getenv("AIS_TTY");   /* a file overrides the terminal */
    FILE *tty;
    char line[AIS_LINE_MAX];
    char ans[16];
    long added = 0, seen = 0;
    int warned_old = 0;

#ifdef _WIN32
    tty = fopen(ttypath != NULL ? ttypath : "CONIN$", "r");
#else
    tty = fopen(ttypath != NULL ? ttypath : "/dev/tty", "r");
#endif
    if (tty == NULL)
        die("import-interactively: no terminal for y/N (set AIS_TTY=FILE)");

    /* Same parse as feed_import; the only addition is the prompt. */
    for (;;) {
        char *keys, *val;
        int rl = store_read_line(line, sizeof(line), stdin);

        if (rl == 0)
            break;
        if (rl < 0) {                     /* same split-line forgery as feed_import */
            fprintf(stderr, "import: line longer than %d bytes, skipped whole\n",
                    AIS_LINE_MAX - 1);
            continue;
        }

        chomp(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;
        if (!parse_kv_line(line, &keys, &val)) {
            int noisy = 0;
            if (strchr(line, '|') == NULL) {
                fprintf(stderr, "import: no -v, skipped: %s\n", line);
                continue;
            }
            feed_old_line(line, &keys, &val, &noisy);
            if (!warned_old) {
                fprintf(stderr, "import: reading pre-v2 '|' lines. The current "
                                "format is KEY... -v VALUE.\n");
                warned_old = 1;
            }
        }
        /* A keyless record is legal (--untag leaves them) and "-v VALUE" states
         * it outright. */
        seen++;

        fprintf(stderr, "%s | %s\n  take into your index? [y/N] ", keys, val);
        fflush(stderr);
        if (fgets(ans, sizeof(ans), tty) == NULL)
            break;                              /* EOF on the terminal -> done */
        if (ans[0] == 'y' || ans[0] == 'Y') {
            if (ais_put(a, keys, val) < 0)
                die("import-interactively: failed on '%s'", val);
            added++;
        }
    }
    fclose(tty);
    fprintf(stderr, "imported %ld of %ld\n", added, seen);
}

void feed_encrypt(ais *a, const char *keys, int from_stdin)
{
    char val[1024], pw[1024], marked[4096];
    long vn, mn, id;

    if (from_stdin) {                      /* -v - : the value comes from stdin */
        size_t got = fread(val, 1, sizeof val - 1, stdin);
        /* The buffer filled: is there more? Storing the prefix would encrypt a
         * secret silently truncated at 1023 bytes, so refuse (the same guard
         * feed_encrypt_doc applies below). A single trailing newline is not
         * "more": it is the terminator a pipe ordinarily appends. */
        if (got == sizeof val - 1) {
            int c = fgetc(stdin);
            if (c == '\n')
                c = fgetc(stdin);
            if (c != EOF) {
                secret_wipe(val, sizeof val);
                die("-e: secret longer than %d bytes -- use --doc -e for a large secret",
                    (int)sizeof val - 1);
            }
        }
        if (got > 0 && val[got - 1] == '\n')
            got--;                         /* a one-line piped secret */
        val[got] = '\0';
        vn = (long)got;
    } else {                               /* prompt for it, echo off, off the tty */
        vn = secret_prompt("secret value: ", 0, val, sizeof val);
    }
    if (vn < 0)
        die("-e: no value (or crypto not built; run crypto/vendor-monocypher.sh)");

    if (secret_prompt("passphrase: ", 1, pw, sizeof pw) < 0) {
        secret_wipe(val, sizeof val);
        die("-e: passphrase entry failed");
    }

    mn = secret_encrypt((const unsigned char *)val, (size_t)vn,
                        (const unsigned char *)pw, strlen(pw), marked, sizeof marked);
    secret_wipe(val, sizeof val);
    secret_wipe(pw, sizeof pw);
    if (mn < 0)
        die("-e: encryption failed (value too large to inline? big notes: use --doc)");

    id = ais_put(a, keys, marked);
    secret_wipe(marked, sizeof marked);
    if (id < 0)
        die("-e: store failed");
    printf("%ld\n", id);
}

void feed_encrypt_doc(ais *a, const char *keys)
{
    enum { DOC_MAX = 4 * 1024 * 1024 };       /* a secret note, not a media file */
    unsigned char *plain;
    char pw[1024];
    char relval[AIS_PATH_MAX], blobpath[AIS_PATH_MAX], marked[AIS_PATH_MAX + 8];
    size_t got;
    long id;

    plain = malloc(DOC_MAX);
    if (plain == NULL)
        die("--doc -e: out of memory");
    got = fread(plain, 1, DOC_MAX, stdin);
    if (got == DOC_MAX && fgetc(stdin) != EOF) {  /* overflowed the cap */
        secret_wipe(plain, DOC_MAX); free(plain);
        die("--doc -e: document exceeds %d bytes; split it", DOC_MAX);
    }
    if (got == 0) {
        free(plain);
        die("--doc -e: empty document on stdin");
    }

    if (secret_prompt("passphrase: ", 1, pw, sizeof pw) < 0) {
        secret_wipe(plain, DOC_MAX); free(plain);
        die("--doc -e: passphrase entry failed (or crypto not built)");
    }
    if (ais_doc_blobname_ext(a, "aisc", relval, sizeof relval, blobpath, sizeof blobpath) != 0) {
        secret_wipe(plain, DOC_MAX); free(plain); secret_wipe(pw, sizeof pw);
        die("--doc -e: cannot prepare blob path under '%s'", a->dir);
    }
    if (secret_encrypt_to_file(plain, got, (const unsigned char *)pw, strlen(pw), blobpath) != 0) {
        secret_wipe(plain, DOC_MAX); free(plain); secret_wipe(pw, sizeof pw);
        die("--doc -e: encryption failed");
    }
    secret_wipe(plain, got); free(plain); secret_wipe(pw, sizeof pw);

    snprintf(marked, sizeof marked, "%s@%s", AIS_SECRET_PREFIX, relval);  /* aisc:@blobs/<ts>.aisc */
    id = ais_put(a, keys, marked);
    if (id < 0) {
        secret_shred_blob(a->dir, marked);    /* don't leave an orphan ciphertext file */
        die("--doc -e: store failed");
    }
    printf("%ld\n", id);
}
