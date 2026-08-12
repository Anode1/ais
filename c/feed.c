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
static int feed_take_blob(ais *a, const char *rel, long want, FILE *in)
{
    char path[AIS_PATH_MAX], dirp[AIS_PATH_MAX], buf[8192];
    const char *base = rel;
    FILE *out = NULL;
    long left = want;

    if (strncmp(rel, "blobs/", 6) == 0)
        base = rel + 6;
    if (strchr(base, '/') != NULL || strchr(base, '\\') != NULL || base[0] == '\0' ||
        strcmp(base, "..") == 0)
        base = NULL;                            /* refuse, but still drain */

    if (base != NULL &&
        snprintf(dirp, sizeof dirp, "%s/blobs", a->dir) < (int)sizeof dirp &&
        snprintf(path, sizeof path, "%s/blobs/%s", a->dir, base) < (int)sizeof path) {
        mkdir(dirp, 0777);      /* win.h maps this to _mkdir */
        out = fopen(path, "rb");                /* already here: keep ours, drain theirs */
        if (out != NULL) { fclose(out); out = NULL; }
        else out = fopen(path, "wb");
    }
    while (left > 0) {
        size_t chunk = (left > (long)sizeof buf) ? sizeof buf : (size_t)left;
        size_t n = fread(buf, 1, chunk, in);
        if (n == 0)
            break;
        if (out != NULL && fwrite(buf, 1, n, out) != n) { fclose(out); out = NULL; }
        left -= (long)n;
    }
    if (out != NULL && fclose(out) != 0)
        return -1;
    return (base == NULL || left != 0) ? -1 : 0;
}

void feed_import_from(ais *a, FILE *in)
{
    char line[AIS_LINE_MAX];
    ais_del_fact pend[AIS_MERGE_BATCH];   /* D| facts awaiting one shared store pass */
    int  npend = 0;
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
                ats = ts[0] ? ts : NULL;
                if (cts[0] != '\0') {          /* a C| named this record's true time */
                    char h[17];
                    content_hash(v, h);
                    if (strcmp(h, chash) == 0)
                        ats = cts;
                    cts[0] = '\0';             /* one A| spends the slot either way */
                }
                if (ais_put_at_k(a, k, v, ts[0] ? ts : NULL, ats) >= 0)
                    n++;
                continue;
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
                if (want >= 0 && feed_take_blob(a, rel, want, in) != 0)
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
                ais_merge_addval(a, h, v);
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
    if (npend > 0)                                     /* end of stream: the last batch */
        ais_merge_del_many(a, pend, npend);
    if (natt > 0)                                      /* likewise the pending attaches */
        ais_merge_attach_many(a, patt, natt);
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
            fprintf(E->out, "M|%s|%s|%s\n", ts, h, value);
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
static int export_blobs_stream(FILE *out, const char *dir, size_t cap)
{
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
        if (snprintf(path, sizeof path, "%s/%s", blobsdir, de->d_name) >= (int)sizeof path)
            continue;
        bf = fopen(path, "rb");
        if (bf == NULL)
            continue;
        if (fseek(bf, 0, SEEK_END) != 0 || (sz = ftell(bf)) < 0 ||
            fseek(bf, 0, SEEK_SET) != 0) { fclose(bf); continue; }
        if (cap > 0 && (total > cap || (size_t)sz > cap - total)) {
            fclose(bf);
            fprintf(stderr, "sync: documents exceed the %lu-byte transfer cap\n",
                    (unsigned long)cap);
            rc = -1;
            break;
        }
        total += (size_t)sz;
        fprintf(out, "B|blobs/%s|%ld\n", de->d_name, sz);
        while ((n = fread(buf, 1, sizeof buf, bf)) > 0)
            fwrite(buf, 1, n, out);
        fclose(bf);
    }
    closedir(d);
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
    if (export_blobs_stream(out, a->dir, blob_cap) != 0)   /* bodies first: a record may point at one */
        return -1;
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
