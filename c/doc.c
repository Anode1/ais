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
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "doc.h"
#include "secret.h"       /* secret_shred_blob: the encrypted half of ais_doc_discard */
#include "win.h"          /* mkdir shim on native Windows; empty on POSIX */

int ais_doc_blobname_ext(const ais *a, const char *ext, char *relval, size_t rvsz,
                         char *blobpath, size_t bpsz)
{
    char dirpath[AIS_PATH_MAX];
    char ts[32];
    time_t now;
    struct tm *lt;
    int seq;

    if (a == NULL || ext == NULL)
        return -1;
    if (snprintf(dirpath, sizeof(dirpath), "%s/blobs", a->dir) >= (int)sizeof(dirpath))
        return -1;
    if (mkdir(dirpath, 0777) != 0 && errno != EEXIST)
        return -1;

    /* Local timestamp, so blobs/ sorts chronologically. A second document
     * within the same second gets a -N suffix; names stay unique with no
     * hashing. The extension marks the kind (.txt plain, .aisc encrypted). */
    now = time(NULL);
    lt = localtime(&now);
    if (lt == NULL || strftime(ts, sizeof(ts), "%Y-%m-%d-%H%M%S", lt) == 0)
        return -1;
    for (seq = 1; seq < 10000; seq++) {
        if (seq == 1)
            snprintf(relval, rvsz, "blobs/%s.%s", ts, ext);
        else
            snprintf(relval, rvsz, "blobs/%s-%d.%s", ts, seq, ext);
        if (snprintf(blobpath, bpsz, "%s/%s", a->dir, relval) >= (int)bpsz)
            return -1;
        if (access(blobpath, F_OK) != 0)
            return 0;                      /* a free name */
    }
    return -1;                             /* 10000 blobs in one second: give up */
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
