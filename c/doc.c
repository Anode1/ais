/* doc.c -- document (blob) storage, shared by every front-end (see doc.h).
 *
 * A multi-line body does not belong inline in the line-oriented store, so it is
 * written to its own file under blobs/ and the record holds only the relative
 * path. The three GUIs and the CLI all route multi-line input through
 * ais_put_value(), so one paste is one record everywhere.
 *
 * die()-free: a socket server and a linked library must survive a write error,
 * so every path returns -1 instead of exiting. */
#define _POSIX_C_SOURCE 200809L     /* mkdir, access */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "doc.h"
#include "secret.h"       /* secret_shred_blob: the encrypted half of ais_doc_discard */
#include "win.h"          /* mkdir shim on native Windows; empty on POSIX */

#if defined(__has_include) && __has_include("crypto/monocypher.h")
#  define DOC_HAVE_CRYPTO 1
#  include "crypto/ais_crypto.h"   /* aisc_random: rand_s on Windows, urandom elsewhere */
#endif

/* Eight hex digits of randomness that make a blob name unique AT BIRTH.
 *
 * Two devices saving a document in the same second used to mint the same name
 * for different bodies, and the importer's rename then changed the arriving
 * record's VALUE -- which is its identity across devices, so it became a new
 * record on every peer, which minted another name, forever. Documents are like
 * notes in a notes app: one save is one note, kept apart from every other save,
 * so the fix belongs at the mint and not in a merge rule.
 *
 * Per BLOB, not per device: the device id is a stub in builds without the crypto
 * module while blobs are minted in all of them, a per-blob tag needs nothing
 * persisted (a cloned index heals with no help), and no device identity leaks
 * into an index handed to someone else. */
static void blob_tag(char out[9])
{
    unsigned char b[4];
    int got = 0;
#ifdef DOC_HAVE_CRYPTO
    got = (aisc_random(b, sizeof b) == AISC_OK);
#endif
    if (!got) {
        /* No RNG in this build: still unique in practice, and a repeat only
         * falls back on the free-name loop below. */
        static unsigned long counter;
        unsigned long m = (unsigned long)getpid() ^ (unsigned long)time(NULL) ^ (counter += 0x9e3779b9UL);
        b[0] = (unsigned char)(m); b[1] = (unsigned char)(m >> 8);
        b[2] = (unsigned char)(m >> 16); b[3] = (unsigned char)(m >> 24);
    }
    snprintf(out, 9, "%02x%02x%02x%02x", b[0], b[1], b[2], b[3]);
}

int ais_doc_blobname_ext(const ais *a, const char *ext, char *relval, size_t rvsz,
                         char *blobpath, size_t bpsz)
{
    char dirpath[AIS_PATH_MAX];
    char ts[32], tag[9];
    time_t now;
    struct tm *lt;
    int seq;

    if (a == NULL || ext == NULL)
        return -1;
    if (snprintf(dirpath, sizeof(dirpath), "%s/blobs", a->dir) >= (int)sizeof(dirpath))
        return -1;
    if (mkdir(dirpath, 0777) != 0 && errno != EEXIST)
        return -1;

    /* Local timestamp, so blobs/ sorts chronologically, then "~" and a random
     * tag so no two devices ever mint one name for two documents. The -N suffix
     * stays as the local free-name loop. The extension marks the kind (.txt
     * plain, .aisc encrypted). Older, untagged names keep working everywhere and
     * are never rewritten: nothing about this migrates. */
    now = time(NULL);
    lt = localtime(&now);
    if (lt == NULL || strftime(ts, sizeof(ts), "%Y-%m-%d-%H%M%S", lt) == 0)
        return -1;
    blob_tag(tag);
    for (seq = 1; seq < 10000; seq++) {
        if (seq == 1)
            snprintf(relval, rvsz, "blobs/%s~%s.%s", ts, tag, ext);
        else
            snprintf(relval, rvsz, "blobs/%s~%s-%d.%s", ts, tag, seq, ext);
        if (snprintf(blobpath, bpsz, "%s/%s", a->dir, relval) >= (int)bpsz)
            return -1;
        if (access(blobpath, F_OK) != 0)
            return 0;                      /* a free name */
    }
    return -1;                             /* 10000 blobs in one second: give up */
}

/* Do the two files hold exactly the same bytes? Blobs are immutable, so this is
 * the whole question when a name arrives that is already taken. */
static int files_equal(const char *pa, const char *pb)
{
    FILE *fa, *fb;
    struct stat sa, sb;
    char ba[8192], bb[8192];
    int eq = 1;

    if (stat(pa, &sa) != 0 || stat(pb, &sb) != 0 || sa.st_size != sb.st_size)
        return 0;
    fa = fopen(pa, "rb");
    if (fa == NULL) return 0;
    fb = fopen(pb, "rb");
    if (fb == NULL) { fclose(fa); return 0; }
    for (;;) {
        size_t na = fread(ba, 1, sizeof ba, fa);
        size_t nb = fread(bb, 1, sizeof bb, fb);
        if (na != nb || (na > 0 && memcmp(ba, bb, na) != 0)) { eq = 0; break; }
        if (na == 0) break;
    }
    if (ferror(fa) || ferror(fb)) eq = 0;
    fclose(fa); fclose(fb);
    return eq;
}

/* FNV-1a over the body, as 16 hex digits. Used to name a body whose preferred
 * name is taken by DIFFERENT content: derived from the bytes alone, so two
 * devices resolving the same clash land on the same name with nothing shared
 * between them, and the mesh settles instead of minting a new pair every round. */
static int blob_body_hash(const char *path, char out[17])
{
    unsigned long long h = 1469598103934665603ULL;   /* FNV-1a 64 offset basis */
    unsigned char buf[8192];
    size_t n;
    FILE *f = fopen(path, "rb");

    if (f == NULL)
        return -1;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        size_t i;
        for (i = 0; i < n; i++) {
            h ^= (unsigned long long)buf[i];
            h *= 1099511628211ULL;
        }
    }
    if (ferror(f)) { fclose(f); return -1; }
    fclose(f);
    snprintf(out, 17, "%016llx", h);
    return 0;
}

/* Place an arriving document body (already written to TMPPATH) under <dir>/blobs/,
 * and report the relative value the record should carry in OUTREL.
 *
 * Blobs are immutable and identified by OCCURRENCE, like notes in a notes app:
 * two people writing the same words wrote two notes, and one save is never
 * merged into another. So an existing file is never overwritten, and both bodies
 * are kept:
 *
 *     name free                    -> take it, the record's value is unchanged
 *     name taken, same bytes       -> one document that arrived twice: drop the copy
 *     name taken, different bytes  -> blobs/<stem>~<body hash><ext>, both kept
 *
 * Only a name minted before blob names carried a random tag can reach the third
 * case. TMPPATH is consumed either way (renamed into place, or unlinked).
 * Returns 0, or -1 leaving TMPPATH for the caller to clean up. */
int ais_doc_blob_place(const char *dir, const char *rel, const char *tmppath,
                       char *outrel, size_t osz)
{
    char blobsdir[AIS_PATH_MAX], target[AIS_PATH_MAX], cand[AIS_PATH_MAX];
    char hash[17];
    const char *base, *dot;
    int stemlen, seq;

    if (dir == NULL || rel == NULL || tmppath == NULL || outrel == NULL)
        return -1;
    if (strncmp(rel, "blobs/", 6) != 0)
        return -1;
    base = rel + 6;
    if (base[0] == '\0' || strstr(base, "..") != NULL ||
        strchr(base, '/') != NULL || strchr(base, '\\') != NULL)
        return -1;                                  /* keep the write inside blobs/ */
    if (snprintf(blobsdir, sizeof blobsdir, "%s/blobs", dir) >= (int)sizeof blobsdir)
        return -1;
    if (mkdir(blobsdir, 0777) != 0 && errno != EEXIST)
        return -1;
    if (snprintf(target, sizeof target, "%s/%s", blobsdir, base) >= (int)sizeof target)
        return -1;

    if (access(target, F_OK) != 0) {                /* free: the ordinary case */
        if (rename(tmppath, target) != 0)
            return -1;
        return (snprintf(outrel, osz, "blobs/%s", base) < (int)osz) ? 0 : -1;
    }
    if (files_equal(target, tmppath)) {             /* the same document, again */
        unlink(tmppath);
        return (snprintf(outrel, osz, "blobs/%s", base) < (int)osz) ? 0 : -1;
    }

    if (blob_body_hash(tmppath, hash) != 0)
        return -1;
    dot = strrchr(base, '.');
    stemlen = (dot != NULL) ? (int)(dot - base) : (int)strlen(base);
    for (seq = 0; seq < 10000; seq++) {
        int k;
        if (seq == 0)
            k = snprintf(cand, sizeof cand, "blobs/%.*s~%s%s", stemlen, base, hash,
                         dot ? dot : "");
        else                                        /* a hash collision: local, rare */
            k = snprintf(cand, sizeof cand, "blobs/%.*s~%s-%d%s", stemlen, base, hash,
                         seq, dot ? dot : "");
        if (k >= (int)sizeof cand)
            return -1;
        if (snprintf(target, sizeof target, "%s/%s", dir, cand) >= (int)sizeof target)
            return -1;
        if (access(target, F_OK) != 0) {
            if (rename(tmppath, target) != 0)
                return -1;
            return (snprintf(outrel, osz, "%s", cand) < (int)osz) ? 0 : -1;
        }
        if (files_equal(target, tmppath)) {         /* already here under that name */
            unlink(tmppath);
            return (snprintf(outrel, osz, "%s", cand) < (int)osz) ? 0 : -1;
        }
    }
    return -1;
}

/* The arriving-name -> local-name map an import builds while placing bodies, and
 * applies ONCE to each record value. Applying it per map entry over the whole
 * text instead was how a value could be rewritten twice (X -> X-1 -> X-1-1),
 * leaving records pointing at the wrong body. */
int ais_blobmap_add(ais_blobmap *m, const char *from, const char *to)
{
    if (m == NULL || from == NULL || to == NULL)
        return -1;
    if (m->n == m->cap) {
        int cap = (m->cap == 0) ? 8 : m->cap * 2;
        char **f = realloc(m->from, (size_t)cap * sizeof *f);
        char **t = realloc(m->to, (size_t)cap * sizeof *t);
        if (f != NULL) m->from = f;
        if (t != NULL) m->to = t;
        if (f == NULL || t == NULL)
            return -1;
        m->cap = cap;
    }
    m->from[m->n] = strdup(from);
    m->to[m->n] = strdup(to);
    if (m->from[m->n] == NULL || m->to[m->n] == NULL) {
        free(m->from[m->n]); free(m->to[m->n]);
        return -1;
    }
    m->n++;
    return 0;
}

const char *ais_blobmap_get(const ais_blobmap *m, const char *from)
{
    int i;
    if (m == NULL || from == NULL)
        return NULL;
    for (i = 0; i < m->n; i++)
        if (strcmp(m->from[i], from) == 0)
            return m->to[i];
    return NULL;
}

void ais_blobmap_free(ais_blobmap *m)
{
    int i;
    if (m == NULL)
        return;
    for (i = 0; i < m->n; i++) { free(m->from[i]); free(m->to[i]); }
    free(m->from); free(m->to);
    m->from = NULL; m->to = NULL; m->n = m->cap = 0;
}

int ais_doc_blobname(const ais *a, char *relval, size_t rvsz,
                     char *blobpath, size_t bpsz)
{
    return ais_doc_blobname_ext(a, "txt", relval, rvsz, blobpath, bpsz);
}

/* The blobs/ test on a bare dir, so ais_doc_discard can serve a value callback
 * that carries the dir rather than the handle. */
static int doc_blob_path(const char *dir, const char *value, char *path, size_t psz)
{
    int n;
    if (dir == NULL || value == NULL
        || strncmp(value, "blobs/", 6) != 0    /* our out-of-line store, not a URL/bookmark */
        || strstr(value, "..") != NULL)        /* never escape the index dir */
        return 0;
    n = snprintf(path, psz, "%s/%s", dir, value);
    return (n > 0 && (size_t)n < psz) ? 1 : 0;
}

int ais_doc_is_blob(const ais *a, const char *value, char *path, size_t psz)
{
    return (a != NULL) ? doc_blob_path(a->dir, value, path, psz) : 0;
}

void ais_doc_discard(const char *index_dir, const char *value)
{
    char path[AIS_PATH_MAX];

    /* Encrypted first: its marker is not a blobs/ path, and it carries its own
     * promise (zero-filled, then unlinked). */
    secret_shred_blob(index_dir, value);
    if (doc_blob_path(index_dir, value, path, sizeof path))
        remove(path);
    /* Anything else is a reference to a file this index did not create. Nothing
     * happens to it, ever. */
}

void ais_doc_discard_cb(const char *value, void *index_dir)
{
    ais_doc_discard((const char *)index_dir, value);
}

long ais_doc_display(const ais *a, const char *value, char *out, size_t outsz)
{
    char path[AIS_PATH_MAX];
    FILE *f;
    size_t cap, got;

    if (out == NULL || outsz == 0)
        return -1;
    out[0] = '\0';
    if (!ais_doc_is_blob(a, value, path, sizeof path)) {
        if (value != NULL)                 /* inline text / URL / secret: verbatim */
            snprintf(out, outsz, "%s", value);
        return -1;
    }
    f = fopen(path, "rb");
    if (f == NULL) {                       /* absent (e.g. not synced here) */
        snprintf(out, outsz, "%s", value); /* fall back to the path; viewer badges it */
        return -1;
    }
    /* leave room for a NUL and, if truncated, a 3-byte "…" (U+2026) marker */
    cap = outsz > 4 ? outsz - 4 : 0;
    got = fread(out, 1, cap, f);
    if (got == cap && cap > 0) {           /* more remained? probe one byte */
        char probe;
        if (fread(&probe, 1, 1, f) == 1) {
            memcpy(out + got, "\xE2\x80\xA6", 3);
            got += 3;
        }
    }
    fclose(f);
    out[got] = '\0';
    return (long)got;
}

long ais_doc_put(ais *a, const char *keys, const char *content, size_t len)
{
    char blobpath[AIS_PATH_MAX], relval[AIS_PATH_MAX];
    FILE *bf;

    if (a == NULL || keys == NULL || content == NULL)
        return -1;
    if (ais_doc_blobname(a, relval, sizeof relval, blobpath, sizeof blobpath) != 0)
        return -1;

    bf = fopen(blobpath, "w");
    if (bf == NULL)
        return -1;
    /* Every failure from here on removes the file it just made: a blob no record
     * points at is the user's document left on disk with nothing able to recall
     * or delete it. feed.c does the same for the encrypted twin. */
    if (len > 0 && fwrite(content, 1, len, bf) != len) {
        fclose(bf);
        remove(blobpath);
        return -1;
    }
    if (fclose(bf) != 0) {
        remove(blobpath);
        return -1;
    }

    {
        long id = ais_put(a, keys, relval);   /* store only the path */
        if (id < 0)
            remove(blobpath);
        return id;
    }
}

/* A newline is "interior" when real content follows it; a lone trailing
 * newline (or trailing blanks) does not make a one-line paste multi-line. */
static int has_interior_newline(const char *s)
{
    const char *nl = strchr(s, '\n');

    if (nl == NULL)
        return 0;
    for (nl++; *nl != '\0'; nl++)
        if (*nl != '\n' && *nl != '\r' && *nl != ' ' && *nl != '\t')
            return 1;
    return 0;
}

long ais_put_value(ais *a, const char *keys, const char *value)
{
    char line[AIS_LINE_MAX];
    size_t n;

    if (a == NULL || keys == NULL || value == NULL)
        return -1;

    /* Multi-line: keep it whole in a blob, preserving the line breaks. */
    if (has_interior_newline(value))
        return ais_doc_put(a, keys, value, strlen(value));

    /* Single line: trim a trailing newline/blank so the store stays one clean
     * line per record. An over-long single line cannot be a store line, so it
     * falls back to a blob rather than being truncated. */
    n = strlen(value);
    while (n > 0 && (value[n - 1] == '\n' || value[n - 1] == '\r' ||
                     value[n - 1] == ' '  || value[n - 1] == '\t'))
        n--;
    if (n == 0)
        return -1;                         /* nothing to store */
    if (n >= sizeof line)
        return ais_doc_put(a, keys, value, strlen(value));
    memcpy(line, value, n);
    line[n] = '\0';
    return ais_put(a, keys, line);
}
