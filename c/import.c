/* import.c -- importers for files other programs wrote (see import.h): browser
 * bookmarks (Netscape HTML) and Google Keep notes (Takeout JSON). feed.c reads
 * ais's own formats; this file reads a foreign one, once, on the way in.
 *
 * Parsing is line/forward streaming over fixed buffers: no DOM, no JSON
 * library, no heap. The puts ride --import's batched store pass
 * (feed_import_stream), so a big import costs one pass per batch rather than
 * one store scan per record; a multi-line Keep note goes out of line through
 * the same blob path every GUI paste takes (ais_put_value, doc.c).
 *
 * Front-end code: it may die() on an unusable argument, the same as feed.c. */
#define _POSIX_C_SOURCE 200809L      /* stat, opendir */
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "doc.h"       /* ais_put_value: a multi-line note becomes a blob */
#include "feed.h"      /* feed_import_stream, feed_import_report */
#include "import.h"
#include "key.h"       /* key_encode: keys carry the engine's own normalization */
#include "log.h"

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

/* Spool one record as a timestampless A| line for feed_import_stream (the put
 * is stamped "now" when the batch applies), or count it skipped when the line
 * would not read back whole. */
static void spool_put(FILE *sp, const char *keys, const char *val, long *skipped)
{
    if (strlen(keys) + strlen(val) + AIS_TS_MAX + 8 >= AIS_LINE_MAX) {
        (*skipped)++;
        return;
    }
    fprintf(sp, "A||%s|%s\n", keys, val);
}

/* ---- browser bookmarks (Netscape HTML) ---------------------------------- */

enum { BM_DEPTH_MAX = 32 };        /* folder nesting; deeper adds no more keys */

long import_bookmarks(ais *a, const char *path)
{
    FILE *in, *sp;
    char line[AIS_LINE_MAX];
    char url[AIS_LINE_MAX], val[AIS_LINE_MAX];
    char title[4096];
    char keys[AIS_LINE_MAX];       /* the folder path's tokens; </DL> truncates */
    char ekeys[AIS_LINE_MAX];
    size_t klen[BM_DEPTH_MAX];
    int depth = 0;
    long n, skipped = 0;

    in = fopen(path, "r");
    if (in == NULL)
        die("--import-bookmarks: cannot open '%s'", path);
    sp = tmpfile();
    if (sp == NULL) {
        fclose(in);
        die("--import-bookmarks: cannot make a temp file");
    }

    /* The file is a nested list: <DT><H3>name</H3> opens a folder, its entries
     * follow inside a <DL>, </DL> closes it, <DT><A HREF="...">title</A> is one
     * bookmark. One tag per line in every real export; a line longer than the
     * buffer (a data: ICON attribute, usually) arrives in chunks -- HREF sits at
     * the front of the tag, so the entry still imports, at worst without its
     * title, and a chunk holding no tag falls through every test below. */
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
            const char *h, *e = NULL;
            gt = strchr(p, '>');
            /* HREF= anywhere inside the tag (attributes come in any order;
             * ADD_DATE, ICON and the rest are ignored), either quote style. */
            h = ci_find(p, "href=");
            if (h != NULL && (gt == NULL || h < gt) &&
                (h[5] == '\"' || h[5] == '\''))
                e = strchr(h + 6, h[5]);
            if (e == NULL) {
                skipped++;                                 /* a malformed <A> */
                continue;
            }
            snprintf(url, sizeof url, "%.*s", (int)(e - h - 6), h + 6);
            import_html_entities(url, url, sizeof url);
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
                if (title[0] != '\0' && u < sizeof val)
                    snprintf(val + u, sizeof val - u, " %s", title);
            }
            /* keys: the folder path's tokens plus the marker; a root-level
             * bookmark gets just "bookmark" */
            snprintf(ekeys, sizeof ekeys, "%s", keys);
            import_keys_add(ekeys, sizeof ekeys, "bookmark");
            spool_put(sp, ekeys, val, &skipped);
            continue;
        }

        for (p = line; (p = ci_find(p, "</dl")) != NULL; p += 4)
            if (depth > 0) {                               /* a folder closes */
                depth--;
                if (depth < BM_DEPTH_MAX)
                    keys[klen[depth]] = '\0';
            }
    }
    fclose(in);

    rewind(sp);
    n = feed_import_stream(a, sp, &skipped);
    fclose(sp);
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
        spool_put(sp, K.keys, val, skipped);
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
    sp = tmpfile();
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
