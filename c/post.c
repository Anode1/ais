/* post.c -- per-key posting lists. See post.h. Uses key.c for placement. */
#define _DEFAULT_SOURCE      /* mkdir */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "key.h"
#include "post.h"
#include "win.h"          /* mkdir shim on native Windows; empty on POSIX */

/* mkdir PATH, tolerating an existing directory. Returns 0, or -1 on error. */
static int post_mkdir(const char *path)
{
    if (mkdir(path, 0777) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

/* Build idx/<p>/<key> for (raw) KEY into OUT, creating idx/ and idx/<p>/ when
 * MAKE is nonzero. Returns 0, -1 on error, 1 if the key encodes empty (skip). */
static int post_path(const ais *a, const char *key, int make,
                     char *out, size_t outsz)
{
    char enc[AIS_KEY_MAX];
    char pre[3];
    char dir[AIS_PATH_MAX];
    int n;

    if (key_encode(key, enc, sizeof(enc)) != 0)
        return 1;
    if (key_prefix(enc, pre, sizeof(pre)) != 0)
        return 1;

    n = snprintf(dir, sizeof(dir), "%s/idx", a->dir);
    if (n < 0 || (size_t)n >= sizeof(dir))
        return -1;
    if (make && post_mkdir(dir) != 0)
        return -1;

    n = snprintf(dir, sizeof(dir), "%s/idx/%s", a->dir, pre);
    if (n < 0 || (size_t)n >= sizeof(dir))
        return -1;
    if (make && post_mkdir(dir) != 0)
        return -1;

    n = snprintf(out, outsz, "%s/idx/%s/%s", a->dir, pre, enc);
    if (n < 0 || (size_t)n >= outsz)
        return -1;
    return 0;
}

int post_append(const ais *a, const char *key, long id)
{
    char path[AIS_PATH_MAX];
    FILE *fp;
    int rc;

    rc = post_path(a, key, 1, path, sizeof(path));
    if (rc != 0)
        return (rc > 0) ? 0 : -1;   /* empty key: nothing to file under */

    fp = fopen(path, "a");
    if (fp == NULL)
        return -1;
    fprintf(fp, "%ld\n", id);
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

int post_insert(const ais *a, const char *key, long id)
{
    char path[AIS_PATH_MAX];
    char tmp[AIS_PATH_MAX];
    char line[64];
    FILE *in, *out;
    long last = 0;
    int seen = 0, written = 0, rc;

    rc = post_path(a, key, 1, path, sizeof(path));
    if (rc != 0)
        return (rc > 0) ? 0 : -1;

    in = fopen(path, "r");
    if (in == NULL)
        return post_append(a, key, id);   /* no file yet -> plain append */

    /* peek: if every existing id is < ours, an append keeps it sorted */
    while (fgets(line, sizeof(line), in) != NULL) {
        long v = atol(line);
        if (v == id) { fclose(in); return 0; }   /* already present */
        last = v;
        seen = 1;
    }
    if (!seen || id > last) {
        fclose(in);
        return post_append(a, key, id);
    }
    rewind(in);

    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        fclose(in);
        return -1;
    }
    out = fopen(tmp, "w");
    if (out == NULL) {
        fclose(in);
        return -1;
    }
    while (fgets(line, sizeof(line), in) != NULL) {
        long v = atol(line);
        if (!written && v > id) {
            fprintf(out, "%ld\n", id);
            written = 1;
        }
        if (v != id)
            fprintf(out, "%ld\n", v);
        else
            written = 1;   /* duplicate guard */
    }
    if (!written)
        fprintf(out, "%ld\n", id);
    fclose(in);
    if (fclose(out) != 0 || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int post_open(const ais *a, const char *key, post_stream *s)
{
    char path[AIS_PATH_MAX];
    int rc;

    s->fp = NULL;
    s->head = 0;
    s->alive = 0;

    rc = post_path(a, key, 0, path, sizeof(path));
    if (rc < 0)
        return -1;
    if (rc > 0)
        return 0;   /* empty key -> empty stream */

    s->fp = fopen(path, "r");
    if (s->fp == NULL)
        return 0;   /* no postings for this key yet -> empty stream */

    return post_next(s);
}

int post_next(post_stream *s)
{
    char line[64];

    if (s->fp == NULL) {
        s->alive = 0;
        return 0;
    }
    if (fgets(line, sizeof(line), s->fp) == NULL) {
        s->alive = 0;
        return 0;
    }
    s->head = atol(line);
    s->alive = 1;
    return 0;
}

void post_close(post_stream *s)
{
    if (s->fp != NULL) {
        fclose(s->fp);
        s->fp = NULL;
    }
    s->alive = 0;
}
