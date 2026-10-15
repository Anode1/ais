/* import.c -- importers for files other programs wrote (see import.h): links
 * (a browser's bookmarks HTML, or a CSV from Pocket, Instapaper, Raindrop or a
 * spreadsheet) and Google Keep notes (Takeout JSON). feed.c reads ais's own
 * formats; this file reads a foreign one, once, on the way in.
 *
 * Parsing is streaming over fixed buffers: no DOM, no JSON or CSV library, no
 * heap. Each parser spools A| lines to a temp file from its own frame and
 * returns; the puts then ride --import's batched store pass
 * (feed_import_stream) on a stack the parser's buffers have left, which is
 * what keeps the whole path inside the FFI seam's budget (tests/stack). A
 * multi-line Keep note goes out of line through the same blob path every GUI
 * paste takes (ais_put_value, doc.c).
 *
 * import_links_fp and the parsers return codes, as the engine does, so the
 * FFI seam can call them; import_bookmarks and import_keep are the CLI's and
 * may die() on an unusable argument, the same as feed.c. */
#define _POSIX_C_SOURCE 200809L      /* stat, opendir */
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>   /* strtoll */
#include <string.h>
#include <time.h>     /* gmtime, strftime: an export's epoch as a store stamp */

#include "doc.h"       /* ais_put_value: a multi-line note becomes a blob */
#include "feed.h"      /* feed_import_stream, feed_import_report */
#include "import.h"
#include "win.h"      /* ais_tmpfile */
#include "key.h"       /* key_encode: keys carry the engine's own normalization */
#include "log.h"
#include "store.h"     /* store_looks_like_ts */

/* Case-insensitive search for an ASCII NEEDLE (exports differ in tag case). */
static const char *ci_find(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);

    for (; *hay != '\0'; hay++) {
        size_t i;
        for (i = 0; i < nl; i++) {
            char h = hay[i], w = needle[i];
            if (h >= 'A' && h <= 'Z') h = (char)(h + ('a' - 'A'));
            if (w >= 'A' && w <= 'Z') w = (char)(w + ('a' - 'A'));
            if (h != w)
                break;
        }
        if (i == nl)
            return hay;
    }
    return NULL;
}

/* Is ENC already one of the space-separated tokens in KEYS? */
static int keys_has_token(const char *keys, const char *enc)
{
    size_t el = strlen(enc);
    const char *p = keys;

    while ((p = strstr(p, enc)) != NULL) {
        if ((p == keys || p[-1] == ' ') && (p[el] == '\0' || p[el] == ' '))
            return 1;
        p++;
    }
    return 0;
}

int import_keys_add(char *keys, size_t ksz, const char *name)
{
    size_t used = strlen(keys);
    const char *p = name;

    while (*p != '\0') {
        char tok[AIS_KEY_MAX], enc[AIS_KEY_MAX];
        size_t tl = 0;
        int w;

        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (*p == '\0')
            break;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            if (tl + 1 < sizeof tok)
                tok[tl++] = *p;
            p++;
        }
        tok[tl] = '\0';
        if (key_encode(tok, enc, sizeof enc) != 0)
            continue;                      /* encodes empty: nothing to file under */
        if (keys_has_token(keys, enc))
            continue;                      /* the same word twice ("Work / work") */
        w = snprintf(keys + used, ksz - used, "%s%s", used ? " " : "", enc);
        if (w < 0 || used + (size_t)w >= ksz) {
            keys[used] = '\0';
            return -1;
        }
        used += (size_t)w;
    }
    return 0;
}

void import_html_entities(const char *in, char *out, size_t osz)
{
    static const struct { const char *ent; size_t n; char ch; } tab[] = {
        { "&amp;",  5, '&'  }, { "&lt;",  4, '<' }, { "&gt;", 4, '>' },
        { "&quot;", 6, '\"' }, { "&#39;", 5, '\'' },
    };
    size_t o = 0;

    /* Decoding never grows, so IN may be OUT: the write index trails the read. */
    while (*in != '\0' && o + 1 < osz) {
        if (*in == '&') {
            size_t k;
            for (k = 0; k < sizeof tab / sizeof tab[0]; k++)
                if (strncmp(in, tab[k].ent, tab[k].n) == 0)
                    break;
            if (k < sizeof tab / sizeof tab[0]) {
                out[o++] = tab[k].ch;
                in += tab[k].n;
                continue;
            }
        }
        out[o++] = *in++;
    }
    out[o] = '\0';
}

/* Four hex digits at P, or -1. */
static long hex4(const char *p)
{
    long v = 0;
    int i;

    for (i = 0; i < 4; i++) {
        char c = p[i];
        int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = v * 16 + d;
    }
    return v;
}

/* Append the UTF-8 bytes of CP at OUT[*o], bounded by OSZ. */
static void utf8_put(unsigned long cp, char *out, size_t *o, size_t osz)
{
    char b[4];
    size_t k, w;

    if      (cp < 0x80)    { b[0] = (char)cp; w = 1; }
    else if (cp < 0x800)   { b[0] = (char)(0xC0 | (cp >> 6));
                             b[1] = (char)(0x80 | (cp & 0x3F)); w = 2; }
    else if (cp < 0x10000) { b[0] = (char)(0xE0 | (cp >> 12));
                             b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                             b[2] = (char)(0x80 | (cp & 0x3F)); w = 3; }
    else                   { b[0] = (char)(0xF0 | (cp >> 18));
                             b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                             b[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                             b[3] = (char)(0x80 | (cp & 0x3F)); w = 4; }
    if (*o + w + 1 > osz)
        return;                            /* full: drop the character whole */
    for (k = 0; k < w; k++)
        out[(*o)++] = b[k];
}

const char *import_json_string(const char *p, char *out, size_t osz)
{
    size_t o = 0;

    if (osz == 0 || *p != '\"')
        return NULL;
    for (p++; *p != '\"'; p++) {
        if (*p == '\0')
            return NULL;                   /* never closed */
        if (*p != '\\') {
            if (o + 1 < osz)
                out[o++] = *p;
            continue;
        }
        p++;
        switch (*p) {
        case 'b': if (o + 1 < osz) out[o++] = '\b'; break;
        case 'f': if (o + 1 < osz) out[o++] = '\f'; break;
        case 'n': if (o + 1 < osz) out[o++] = '\n'; break;
        case 'r': if (o + 1 < osz) out[o++] = '\r'; break;
        case 't': if (o + 1 < osz) out[o++] = '\t'; break;
        case 'u': {
            long cp = hex4(p + 1);
            if (cp < 0)
                return NULL;
            p += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF &&
                p[1] == '\\' && p[2] == 'u') {          /* a surrogate pair */
                long lo = hex4(p + 3);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    p += 6;
                }
            }
            utf8_put((unsigned long)cp, out, &o, osz);
            break;
        }
        case '\0': return NULL;
        default:                           /* '"', '\\', '/', or an unknown one */
            if (o + 1 < osz)
                out[o++] = *p;
        }
    }
    out[o] = '\0';
    return p + 1;
}

/* Trim leading and trailing whitespace in place. */
static void trim(char *s)
{
    size_t n = strlen(s), b = 0;

    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
    while (s[b] == ' ' || s[b] == '\t')
        b++;
    if (b > 0)
        memmove(s, s + b, n - b + 1);
}

/* Spool one record as an A| line for feed_import_stream (TS "" stamps the put
 * "now" when the batch applies), or count it skipped when the line would not
 * read back whole. The spool is line framed and its verbs sit at line start,
 * so a control byte in a field is folded to a space first: a title holding
 * "\nD|..." must not become a delete. Keys arrive encoded (key_encode) and
 * carry none. */
static void spool_put(FILE *sp, const char *ts, const char *keys, char *val, long *skipped)
{
    char *p;

    if (strlen(keys) + strlen(val) + AIS_TS_MAX + 8 >= AIS_LINE_MAX) {
        (*skipped)++;
        return;
    }
    for (p = val; *p != '\0'; p++)
        if ((unsigned char)*p < 0x20 || *p == 0x7f)
            *p = ' ';
    fprintf(sp, "A|%s|%s|%s\n", ts, keys, val);
}

/* The export's own save time as the store stamps one (UTC, "...Z"): CELL is
 * an epoch in seconds (milliseconds when far too large for seconds), or an
 * ISO date-time in UTC, a trailing Z or +00:00 (a space for the T allowed).
 * OUT is "" for anything else, a date alone or a local time included, and the
 * put is stamped now: a guessed zone would be a wrong time. */
static void ts_from_cell(const char *cell, char *out, size_t osz)
{
    const char *p = cell;
    size_t n, i;

    out[0] = '\0';
    while (*p == ' ' || *p == '\t')
        p++;
    n = strlen(p);
    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r'))
        n--;
    if (n == 0 || n > 30)
        return;
    for (i = 0; i < n && p[i] >= '0' && p[i] <= '9'; i++)
        ;
    if (i == n) {                                  /* an epoch */
        long long v = strtoll(p, NULL, 10);
        time_t t;
        struct tm *gt;
        if (v > 100000000000LL)
            v /= 1000;                             /* milliseconds */
        t = (time_t)v;
        gt = gmtime(&t);
        if (gt == NULL || strftime(out, osz, "%Y-%m-%dT%H:%M:%SZ", gt) == 0)
            out[0] = '\0';
        return;
    }
    if (n >= 20 && osz > 21 &&
        (p[n - 1] == 'Z' || strcmp(p + n - 6, "+00:00") == 0)) {
        snprintf(out, osz, "%.19s", p);
        if (out[10] == ' ')
            out[10] = 'T';
        if (store_looks_like_ts(out) && strlen(out) == 19) {
            out[19] = 'Z';                         /* fits: osz > 21 checked above */
            out[20] = '\0';
        } else
            out[0] = '\0';
    }
}

static int ci_eq(const char *s, const char *w)
{
    return strlen(s) == strlen(w) && ci_find(s, w) == s;
}

/* Is S (trimmed) one of an app's own buckets, a place rather than a name the
 * user made: Instapaper's Unread / Archive / Starred, Raindrop's Unsorted. */
static int is_bucket(const char *s)
{
    static const char *const b[] = { "unread", "archive", "starred", "unsorted" };
    size_t k;

    for (k = 0; k < sizeof b / sizeof b[0]; k++)
        if (ci_eq(s, b[k]))
            return 1;
    return 0;
}

/* Does a word of header cell HEAD (words split at blanks and _ - . ) start
 * with W (WHOLE: equal W)? "Document tags" has one starting "tag"; "Updated"
 * has none starting "date". */
static int head_has(const char *head, const char *w, int whole)
{
    const char *p = head;
    size_t wl = strlen(w);

    for (;;) {
        size_t n;
        while (*p == ' ' || *p == '\t' || *p == '_' || *p == '-' || *p == '.')
            p++;
        if (*p == '\0')
            return 0;
        for (n = 0; p[n] != '\0' && p[n] != ' ' && p[n] != '\t' &&
                    p[n] != '_' && p[n] != '-' && p[n] != '.'; n++)
            ;
        if (n >= wl && ci_find(p, w) == p && (!whole || n == wl))
            return 1;
        p += n;
    }
}

/* ---- browser bookmarks (Netscape HTML) ---------------------------------- */

enum { BM_DEPTH_MAX = 32 };        /* folder nesting; deeper adds no more keys */

/* The value of attribute NAME inside the tag at TAG (up to GT, the '>' or the
 * line's end), either quote style, entities decoded, into OUT. 1 if present. */
static int attr_val(const char *tag, const char *gt, const char *name,
                    char *out, size_t osz)
{
    size_t nl = strlen(name);
    const char *h = tag, *e;

    for (;;) {
        h = ci_find(h, name);
        if (h == NULL || (gt != NULL && h >= gt))
            return 0;
        /* a whole attribute name: preceded by a blank, followed by = and a quote */
        if (h > tag && (h[-1] == ' ' || h[-1] == '\t') && h[nl] == '=' &&
            (h[nl + 1] == '\"' || h[nl + 1] == '\''))
            break;
        h += nl;
    }
    e = strchr(h + nl + 2, h[nl + 1]);
    if (e == NULL)
        return 0;
    snprintf(out, osz, "%.*s", (int)(e - h - nl - 2), h + nl + 2);
    import_html_entities(out, out, osz);
    return 1;
}

/* Spool every link in a bookmarks HTML. The file is a nested list: <DT><H3>
 * name</H3> opens a folder, its entries follow inside a <DL>, </DL> closes it,
 * <DT><A HREF="...">title</A> is one bookmark. One tag per line in every real
 * export; a line longer than the buffer (a data: ICON attribute, usually)
 * arrives in chunks, and HREF sits at the front of the tag, so the entry still
 * imports, at worst without its title, and a chunk holding no tag falls
 * through every test below. */
AIS_NOINLINE static long bm_parse(FILE *in, FILE *sp, long *skipped)
{
    char line[AIS_LINE_MAX];
    char url[AIS_LINE_MAX], val[AIS_LINE_MAX];
    char title[4096], tags[4096], date[64], ts[AIS_TS_MAX];
    char keys[AIS_LINE_MAX];       /* the folder path's tokens; </DL> truncates */
    char ekeys[AIS_LINE_MAX];
    size_t klen[BM_DEPTH_MAX];
    int depth = 0;
    long n = 0;

    keys[0] = '\0';
    while (fgets(line, sizeof line, in) != NULL) {
        const char *p, *gt, *end;

        if ((p = ci_find(line, "<h3")) != NULL) {          /* a folder opens */
            if (depth < BM_DEPTH_MAX) {
                klen[depth] = strlen(keys);
                gt  = strchr(p, '>');
                end = (gt != NULL) ? ci_find(gt + 1, "</h3") : NULL;
                if (gt != NULL && end != NULL) {
                    snprintf(title, sizeof title, "%.*s",
                             (int)(end - gt - 1), gt + 1);
                    import_html_entities(title, title, sizeof title);
                    import_keys_add(keys, sizeof keys, title);
                }
            }
            depth++;               /* counted past the cap too, so pops match */
            continue;
        }

        p = ci_find(line, "<a");
        if (p != NULL && (p[2] == ' ' || p[2] == '\t' || p[2] == '>')) {
            gt = strchr(p, '>');
            /* HREF= anywhere inside the tag (attributes come in any order;
             * ICON and the rest are ignored), either quote style. */
            if (!attr_val(p, gt, "href", url, sizeof url)) {
                (*skipped)++;                              /* a malformed <A> */
                continue;
            }
            title[0] = '\0';
            end = (gt != NULL) ? ci_find(gt + 1, "</a") : NULL;
            if (end != NULL) {
                snprintf(title, sizeof title, "%.*s", (int)(end - gt - 1), gt + 1);
                import_html_entities(title, title, sizeof title);
                trim(title);
            }
            /* value: the URL, then a single space and the title if there is one */
            {
                int w = snprintf(val, sizeof val, "%s", url);
                size_t u = (w > 0) ? (size_t)w : 0;
                if (title[0] != '\0' && strcmp(title, url) != 0 && u < sizeof val)
                    snprintf(val + u, sizeof val - u, " %s", title);
            }
            /* keys: the folder path's tokens, the link's own TAGS (Firefox,
             * Raindrop, Pinboard write them), then the marker; a root-level
             * untagged bookmark gets just "bookmark". A record the marker
             * would not fit is skipped: without it the batch is not reversible. */
            snprintf(ekeys, sizeof ekeys, "%s", keys);
            if (attr_val(p, gt, "tags", tags, sizeof tags)) {
                char *c;
                for (c = tags; *c != '\0'; c++)
                    if (*c == ',')
                        *c = ' ';
                import_keys_add(ekeys, sizeof ekeys, tags);
            }
            if (import_keys_add(ekeys, sizeof ekeys, "bookmark") != 0) {
                (*skipped)++;
                continue;
            }
            ts[0] = '\0';
            if (attr_val(p, gt, "add_date", date, sizeof date))
                ts_from_cell(date, ts, sizeof ts);
            spool_put(sp, ts, ekeys, val, skipped);
            n++;
            continue;
        }

        for (p = line; (p = ci_find(p, "</dl")) != NULL; p += 4)
            if (depth > 0) {                               /* a folder closes */
                depth--;
                if (depth < BM_DEPTH_MAX)
                    keys[klen[depth]] = '\0';
            }
    }
    return n;
}

/* ---- links CSV (Pocket, Instapaper, Raindrop, a spreadsheet) ------------- */

enum {
    CSV_URL_MAX    = 8192,         /* a field longer than this: the row is skipped */
    CSV_TITLE_MAX  = 4096,         /* longer: truncated */
    CSV_TAGS_MAX   = 4096,
    CSV_FOLDER_MAX = 1024,
    CSV_TIME_MAX   = 64,
    CSV_HEAD_MAX   = 128,          /* one header cell */
    CSV_COLS_MAX   = 256           /* columns looked at; the rest pass by */
};

/* Read one field of a SEP-separated row from IN into OUT (NULL: discard it),
 * RFC 4180: a quoted field may hold SEP, newlines and doubled quotes. Byte by
 * byte, so a Raindrop note longer than any buffer streams past. Sets *OVER
 * when the field did not fit (the rest is dropped). Returns what ended it:
 * SEP, '\n' (a CR before it dropped) or EOF. */
static int csv_field(FILE *in, int sep, char *out, size_t osz, int *over)
{
    size_t o = 0;
    int c, quoted = 0;

    *over = 0;
    c = getc(in);
    if (c == '"')
        quoted = 1;
    else if (c != EOF)
        ungetc(c, in);
    for (;;) {
        c = getc(in);
        if (c == EOF)
            break;
        if (quoted) {
            if (c == '"') {
                c = getc(in);
                if (c != '"') {        /* closed: run to the terminator */
                    while (c != EOF && c != sep && c != '\n')
                        c = getc(in);
                    break;
                }                      /* doubled: one literal quote */
            }
        } else {
            if (c == sep || c == '\n')
                break;
            if (c == '\r') {
                int d = getc(in);
                if (d == '\n' || d == EOF) { c = d; break; }
                ungetc(d, in);
            }
        }
        if (out != NULL) {
            if (o + 1 < osz) out[o++] = (char)c;
            else *over = 1;
        }
    }
    if (out != NULL)
        out[o] = '\0';
    return (c == EOF) ? EOF : (c == '\n') ? '\n' : sep;
}

/* The separator the header row uses: the most frequent of , ; and TAB outside
 * quotes (a spreadsheet in a de/fr/ru locale saves semicolons). Leaves IN where
 * it was. */
static int csv_sep(FILE *in)
{
    long start = ftell(in);
    int c, q = 0, nc = 0, ns = 0, nt = 0, seen = 0;

    while ((c = getc(in)) != EOF && c != '\n' && seen++ < 8192) {
        if (c == '"') q = !q;
        else if (!q && c == ',') nc++;
        else if (!q && c == ';') ns++;
        else if (!q && c == '\t') nt++;
    }
    fseek(in, start, SEEK_SET);
    if (ns > nc && ns >= nt) return ';';
    if (nt > nc && nt > ns) return '\t';
    return ',';
}

struct csvcols { int url, title, tags, folder, time; };
struct csvrow {
    char url[CSV_URL_MAX], title[CSV_TITLE_MAX], tags[CSV_TAGS_MAX];
    char folder[CSV_FOLDER_MAX], time[CSV_TIME_MAX];
};

/* Spool every row of a links CSV. The header row names the columns and the
 * exporter (the rule is import.h's); the exporter names the marker key, left
 * in *MARKER. Tags split on | , ; and blanks (Pocket writes a|b, Raindrop
 * "a, b", a JSON list ["a","b"] sheds its brackets); a folder path splits on
 * / as well. KEYS holds every tag and folder word (encoding never grows one)
 * with room to spare, so the marker always fits. */
AIS_NOINLINE static long csv_parse(FILE *in, FILE *sp, long *skipped,
                                   const char **marker)
{
    struct csvcols C = { -1, -1, -1, -1, -1 };
    struct csvrow R;
    char head[CSV_HEAD_MAX];
    char keys[CSV_TAGS_MAX + CSV_FOLDER_MAX + 32];
    char val[CSV_URL_MAX + CSV_TITLE_MAX + 2];
    char ts[AIS_TS_MAX];
    int sep = csv_sep(in);
    int term, over, i;
    long n = 0;

    *marker = "link";
    for (i = 0; ; i++) {
        term = csv_field(in, sep, head, sizeof head, &over);
        trim(head);
        if (i < CSV_COLS_MAX) {
            if (C.url < 0 && (head_has(head, "url", 0) ||
                              head_has(head, "href", 1) || head_has(head, "link", 1)))
                C.url = i;
            else if (C.title < 0 && (head_has(head, "title", 0) || head_has(head, "name", 1)))
                C.title = i;
            else if (C.tags < 0 && head_has(head, "tag", 0))
                C.tags = i;
            else if (C.folder < 0 && (head_has(head, "folder", 0) ||
                                      head_has(head, "collection", 0)))
                C.folder = i;
            else if (C.time < 0 && (head_has(head, "timestamp", 0) ||
                                    head_has(head, "created", 0) ||
                                    head_has(head, "added", 1) ||
                                    head_has(head, "date", 1) ||
                                    head_has(head, "saved", 1)))
                C.time = i;
            if (ci_eq(head, "time_added"))     *marker = "pocket";
            else if (ci_eq(head, "selection")) *marker = "instapaper";
            else if (ci_eq(head, "excerpt"))   *marker = "raindrop";
        }
        if (term != sep)
            break;
    }
    if (C.url < 0)
        return AIS_IMPORT_NOURL;
    if (term == EOF)
        return 0;                                  /* a header and nothing under it */

    for (;;) {
        int urlover = 0, cells = 0;
        char *c;

        R.url[0] = R.title[0] = R.tags[0] = R.folder[0] = R.time[0] = '\0';
        for (i = 0; ; i++) {
            char *dst = NULL;
            size_t dsz = 0;
            if      (i == C.url)    { dst = R.url;    dsz = sizeof R.url; }
            else if (i == C.title)  { dst = R.title;  dsz = sizeof R.title; }
            else if (i == C.tags)   { dst = R.tags;   dsz = sizeof R.tags; }
            else if (i == C.folder) { dst = R.folder; dsz = sizeof R.folder; }
            else if (i == C.time)   { dst = R.time;   dsz = sizeof R.time; }
            term = csv_field(in, sep, dst, dsz, &over);
            if (i == C.url && over)
                urlover = 1;
            if (dst != NULL && dst[0] != '\0')
                cells++;
            if (term != sep)
                break;
        }
        if (i > 0 || cells > 0) {                  /* not a blank line */
            trim(R.url);
            if (R.url[0] == '\0' || urlover || strpbrk(R.url, " \t") != NULL) {
                (*skipped)++;                      /* no link, or not one whole */
            } else {
                keys[0] = '\0';
                for (c = R.tags; *c != '\0'; c++)
                    if (*c == '|' || *c == ',' || *c == ';' ||
                        *c == '[' || *c == ']' || *c == '"')
                        *c = ' ';
                import_keys_add(keys, sizeof keys, R.tags);
                trim(R.folder);
                if (!is_bucket(R.folder)) {
                    for (c = R.folder; *c != '\0'; c++)
                        if (*c == '/')
                            *c = ' ';
                    import_keys_add(keys, sizeof keys, R.folder);
                }
                import_keys_add(keys, sizeof keys, *marker);
                trim(R.title);
                if (R.title[0] != '\0' && strcmp(R.title, R.url) != 0)
                    snprintf(val, sizeof val, "%s %s", R.url, R.title);
                else
                    snprintf(val, sizeof val, "%s", R.url);
                ts_from_cell(R.time, ts, sizeof ts);
                spool_put(sp, ts, keys, val, skipped);
                n++;
            }
        }
        if (term == EOF)
            break;
    }
    return n;
}

/* ---- the links entry points --------------------------------------------- */

enum { KIND_HTML = 1, KIND_CSV = 2 };

/* What IN holds, by its first bytes: a zip or UTF-16 text is refused, a UTF-8
 * BOM is stepped over, and the first non-blank byte tells HTML ('<': every
 * Netscape export opens with <!DOCTYPE) from a CSV (no header starts with it). */
static int sniff(FILE *in)
{
    unsigned char b[4];
    size_t got = fread(b, 1, sizeof b, in);
    long start;
    int c;

    if (got == 4 && b[0] == 'P' && b[1] == 'K' && b[2] == 3 && b[3] == 4)
        return AIS_IMPORT_ZIP;
    if (got >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF)))
        return AIS_IMPORT_UTF16;
    if (got >= 2 && b[0] != 0 && b[1] == 0)
        return AIS_IMPORT_UTF16;                   /* BOM-less UTF-16LE text */
    rewind(in);
    if (got >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF)
        fseek(in, 3, SEEK_SET);
    start = ftell(in);
    while ((c = getc(in)) == ' ' || c == '\t' || c == '\r' || c == '\n')
        ;
    fseek(in, start, SEEK_SET);
    return (c == '<') ? KIND_HTML : KIND_CSV;
}

long import_links_fp(ais *a, FILE *in, long *skipped, const char **marker)
{
    FILE *sp;
    const char *m = "bookmark";
    long n;
    int kind = sniff(in);

    if (kind < 0)
        return kind;
    sp = ais_tmpfile();
    if (sp == NULL)
        return AIS_IMPORT_TMP;
    n = (kind == KIND_HTML) ? bm_parse(in, sp, skipped) : csv_parse(in, sp, skipped, &m);
    if (marker != NULL)
        *marker = m;
    if (n <= 0) {
        fclose(sp);
        return n;
    }
    rewind(sp);                /* the parser's frame is gone: now the store pass */
    n = feed_import_stream(a, sp, skipped);
    fclose(sp);
    return n;
}

long import_bookmarks(ais *a, const char *path)
{
    FILE *in = fopen(path, "rb");        /* binary: CRLF is ours, and no ^Z EOF on Windows */
    long n, skipped = 0;

    if (in == NULL)
        die("--import-bookmarks: cannot open '%s'", path);
    n = import_links_fp(a, in, &skipped, NULL);
    fclose(in);
    if (n == AIS_IMPORT_NOURL)
        die("--import-bookmarks: no url column in '%s' (expected a bookmarks HTML, "
            "or a Pocket, Instapaper, Raindrop or other CSV with a url column)", path);
    if (n == AIS_IMPORT_UTF16)
        die("--import-bookmarks: '%s' is UTF-16 text; save it as UTF-8 CSV first", path);
    if (n == AIS_IMPORT_ZIP)
        die("--import-bookmarks: '%s' is a zip; open it, then pass the .csv inside", path);
    if (n < 0)
        die("--import-bookmarks: cannot make a temp file");
    feed_import_report(n, skipped);
    return n;
}

/* ---- Google Keep (Takeout JSON, one file per note) ---------------------- */

enum {
    KEEP_JSON_MAX = 262144,        /* one note file; Keep's own caps sit far below */
    KEEP_TEXT_MAX = 131072
};

struct keep_note {
    char title[4096];
    char content[KEEP_TEXT_MAX];   /* textContent, or the "- item" list lines */
    char keys[AIS_LINE_MAX];       /* label tokens, then the "keep" marker */
    int  trashed;
};

/* One list item becomes one "- item" line of the note's content. */
static void keep_add_item(struct keep_note *K, const char *item)
{
    size_t used = strlen(K->content);
    int w = snprintf(K->content + used, sizeof K->content - used, "%s- %s",
                     used ? "\n" : "", item);

    if (w < 0 || used + (size_t)w >= sizeof K->content)
        K->content[used] = '\0';           /* full: keep what fits, whole lines */
}

/* One forward pass over a note's JSON, no library: depth says whether a key is
 * the note's own (an annotation's "title" or "text" sits deeper and is never
 * read), ARR which of the two arrays the importer follows is open. Only the
 * fields read here exist for this parser; everything else is walked over. */
static void keep_parse(const char *j, struct keep_note *K)
{
    const char *p = j;
    int depth = 0, arr = 0, arr_depth = 0;     /* arr: 1 labels, 2 listContent */
    char key[64], item[4096];

    while (*p != '\0') {
        if (*p == '{' || *p == '[') { depth++; p++; continue; }
        if (*p == '}' || *p == ']') {
            if (arr != 0 && depth == arr_depth && *p == ']')
                arr = 0;
            depth--;
            p++;
            continue;
        }
        if (*p != '\"') { p++; continue; }

        /* a string: a key when ':' follows, otherwise a value in an ignored spot */
        {
            const char *q = import_json_string(p, key, sizeof key);
            if (q == NULL)
                return;                    /* unterminated: keep what was read */
            p = q;
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
                p++;
            if (*p != ':')
                continue;
            p++;
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
                p++;
        }

        if (*p == '\"') {                  /* a string value: route or walk over */
            if (depth == 1 && strcmp(key, "title") == 0)
                p = import_json_string(p, K->title, sizeof K->title);
            else if (depth == 1 && strcmp(key, "textContent") == 0)
                p = import_json_string(p, K->content, sizeof K->content);
            else if (arr == 1 && depth == arr_depth + 1 && strcmp(key, "name") == 0) {
                p = import_json_string(p, item, sizeof item);
                if (p != NULL)
                    import_keys_add(K->keys, sizeof K->keys, item);
            } else if (arr == 2 && depth == arr_depth + 1 && strcmp(key, "text") == 0) {
                p = import_json_string(p, item, sizeof item);
                if (p != NULL)
                    keep_add_item(K, item);
            } else
                p = import_json_string(p, item, sizeof item);
            if (p == NULL)
                return;
            continue;
        }
        if (*p == '[') {                   /* the '[' itself is depth-counted above */
            if (depth == 1 && strcmp(key, "labels") == 0)
                { arr = 1; arr_depth = depth + 1; }
            else if (depth == 1 && strcmp(key, "listContent") == 0)
                { arr = 2; arr_depth = depth + 1; }
            continue;
        }
        if (depth == 1 && strcmp(key, "isTrashed") == 0 && strncmp(p, "true", 4) == 0)
            K->trashed = 1;
        /* numbers, booleans, null, nested objects: the loop walks over them */
    }
}

/* One note file: parse, compose "title\ncontent", store. Counts through N (the
 * multi-line puts only; single-line ones ride the spool) and SKIPPED. */
static void keep_note_file(ais *a, const char *path, FILE *sp,
                           long *n, long *skipped)
{
    char raw[KEEP_JSON_MAX];
    struct keep_note K;
    FILE *f;
    size_t got, used = 0;
    char *val;

    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "import-keep: cannot read %s\n", path);
        (*skipped)++;
        return;
    }
    got = fread(raw, 1, sizeof raw - 1, f);
    if (got == sizeof raw - 1 && fgetc(f) != EOF) {
        fclose(f);
        fprintf(stderr, "import-keep: %s is over %d bytes, skipped\n",
                path, KEEP_JSON_MAX - 1);
        (*skipped)++;
        return;
    }
    fclose(f);
    raw[got] = '\0';
    if (got == 0 || memchr(raw, '\0', got) != NULL) {
        (*skipped)++;                      /* empty or binary: not a note */
        return;
    }

    memset(&K, 0, sizeof K);
    keep_parse(raw, &K);
    if (K.trashed) {                       /* deliberately in the trash: not carried */
        (*skipped)++;
        return;
    }
    import_keys_add(K.keys, sizeof K.keys, "keep");

    /* value: title and content joined; RAW is done with, so it holds the join
     * (always shorter than the JSON it came from) */
    val = raw;
    val[0] = '\0';
    if (K.title[0] != '\0') {
        int w = snprintf(val, sizeof raw, "%s", K.title);
        used = (w > 0) ? (size_t)w : 0;
    }
    if (K.content[0] != '\0')
        snprintf(val + used, sizeof raw - used, "%s%s", used ? "\n" : "", K.content);
    trim(val);
    if (val[0] == '\0') {
        (*skipped)++;                      /* an empty note */
        return;
    }

    if (strchr(val, '\n') != NULL ||
        strlen(val) > (size_t)(AIS_LINE_MAX - AIS_WIRE_FRAME_MAX)) {
        /* multi-line (or too long to inline): out of line through the same
         * blob path every GUI paste takes; the record holds the blobs/ path */
        if (ais_put_value(a, K.keys, val) >= 0)
            (*n)++;
        else
            (*skipped)++;
    } else {
        spool_put(sp, "", K.keys, val, skipped);
    }
}

long import_keep(ais *a, const char *dir)
{
    struct stat st;
    DIR *d;
    struct dirent *de;
    FILE *sp;
    long n = 0, skipped = 0;
    size_t dl = strlen(dir);

    if (stat(dir, &st) != 0)
        die("--import-keep: cannot open '%s'", dir);
    if (!S_ISDIR(st.st_mode)) {
        if (dl > 4 && ci_find(dir + dl - 4, ".zip") != NULL)
            die("--import-keep: extract the .zip first, then pass the Keep folder inside it");
        die("--import-keep: '%s' is not a directory of Keep .json files", dir);
    }
    d = opendir(dir);
    if (d == NULL)
        die("--import-keep: cannot read '%s'", dir);
    sp = ais_tmpfile();
    if (sp == NULL) {
        closedir(d);
        die("--import-keep: cannot make a temp file");
    }
    while ((de = readdir(d)) != NULL) {
        char path[AIS_PATH_MAX];
        size_t bl = strlen(de->d_name);
        if (bl < 5 || strcmp(de->d_name + bl - 5, ".json") != 0)
            continue;                      /* Takeout mixes in .html and media */
        if (snprintf(path, sizeof path, "%s/%s", dir, de->d_name) >= (int)sizeof path)
            continue;
        keep_note_file(a, path, sp, &n, &skipped);
    }
    closedir(d);

    rewind(sp);
    n += feed_import_stream(a, sp, &skipped);
    fclose(sp);
    feed_import_report(n, skipped);
    return n;
}
