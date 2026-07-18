/* store.c -- append-only store, monotonic id, writer lock. See store.h. */
#define _DEFAULT_SOURCE      /* flock, mkdir, fdopen via BSD/POSIX */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/file.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "store.h"
#include "win.h"          /* flock + mkdir shims on native Windows; empty on POSIX */

/* Build "<dir>/<name>" into OUT. Returns 0, or -1 if it would not fit. */
static int store_path(const ais *a, const char *name, char *out, size_t outsz)
{
    int n = snprintf(out, outsz, "%s/%s", a->dir, name);
    if (n < 0 || (size_t)n >= outsz)
        return -1;
    return 0;
}

/* True if P[i..i+n) are all ASCII digits. */
static int ts_digits(const char *p, int i, int n)
{
    for (; n > 0; n--, i++)
        if (p[i] < '0' || p[i] > '9')
            return 0;
    return 1;
}

/* Does field-2 of a store line hold a timestamp? This tells a v2 line
 * (id|ts|keys|value) from a legacy v1 line (id|keys|value). We accept the
 * engine's own "YYYY-MM-DDThh:mm:ss" and the hand-written shortenings
 * "YYYY-MM-DD" and "YYYY-MM-DDThh:mm" -- always anchored and ending at '|'/EOL.
 *
 * The lower bound is deliberately a FULL date (YYYY-MM-DD): a bare year or
 * "YYYY-MM" is left as a KEY, because tagging by year ("photos 2026") is
 * common and must not be mistaken for a save time. A malformed date simply
 * fails here and the line is read as a dateless v1 record -- the id, keys and
 * value are never lost, only the date is dropped. */
static int looks_like_ts(const char *p)
{
    int i;

    for (i = 0; i < 10; i++)              /* need at least a full date present */
        if (p[i] == '\0')
            return 0;
    if (p[4] != '-' || p[7] != '-' ||
        !ts_digits(p, 0, 4) || !ts_digits(p, 5, 2) || !ts_digits(p, 8, 2))
        return 0;
    if (p[10] == '|' || p[10] == '\0')
        return 1;                         /* date only:  YYYY-MM-DD */
    if (p[10] != 'T')
        return 0;
    if (p[11] == '\0' || p[12] == '\0' || p[13] != ':' ||
        p[14] == '\0' || p[15] == '\0' ||
        !ts_digits(p, 11, 2) || !ts_digits(p, 14, 2))
        return 0;
    if (p[16] == '|' || p[16] == '\0')
        return 1;                         /* to the minute:  ...Thh:mm */
    if (p[16] != ':' || p[17] == '\0' || p[18] == '\0' || !ts_digits(p, 17, 2))
        return 0;
    if (p[19] == '|' || p[19] == '\0')
        return 1;                         /* to the second:  ...Thh:mm:ss (v2) */
    if (p[19] == 'Z')                     /* UTC:  ...Thh:mm:ssZ (v3) */
        return p[20] == '|' || p[20] == '\0';
    return 0;
}

/* Split a store line in place: "id|ts|keys|value" (v2) or "id|keys|value" (v1,
 * *ts comes back ""). Trims the trailing newline. On success sets *id, *ts,
 * *keys, *value (pointers into LINE) and returns 0. -1 if malformed. */
static int store_parse(char *line, long *id, char **ts, char **keys, char **value)
{
    char *p, *bar;
    size_t len = strlen(line);

    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';

    bar = strchr(line, '|');
    if (bar == NULL)
        return -1;
    *bar = '\0';
    *id = atol(line);
    if (*id <= 0)
        return -1;

    p = bar + 1;                          /* field 2: ts (v2) or keys (v1) */
    if (looks_like_ts(p)) {
        *ts = p;
        bar = strchr(p, '|');
        if (bar == NULL) {                /* ts but no keys/value (degenerate) */
            *keys = p + strlen(p);
            *value = *keys;
            return 0;
        }
        *bar = '\0';
        p = bar + 1;
    } else {
        *ts = line + strlen(line);        /* "" -- a legacy v1 line */
    }

    *keys = p;
    bar = strchr(p, '|');
    if (bar == NULL)
        *value = p + strlen(p);           /* empty value */
    else {
        *bar = '\0';
        *value = bar + 1;
    }
    return 0;
}

int store_now(char *buf, size_t bufsz)
{
    time_t now = time(NULL);
    struct tm *gt = gmtime(&now);          /* UTC: one canonical instant across devices */

    buf[0] = '\0';
    if (gt == NULL || strftime(buf, bufsz, "%Y-%m-%dT%H:%M:%SZ", gt) == 0) {
        buf[0] = '\0';
        return -1;
    }
    return 0;
}

/* Stable content hash of a record's VALUE -- its cross-device identity, since put
 * dedups by value (the same value IS the same record; keys are labels that union on
 * merge). FNV-1a 64-bit as 16 hex chars + NUL. NOT a security hash. Same value ->
 * same hash on any device, independent of local ids. */
void content_hash(const char *value, char out[17])
{
    unsigned long long h = 1469598103934665603ULL;   /* FNV-1a offset basis */
    const char *p;
    for (p = value; *p != '\0'; p++) { h ^= (unsigned char)*p; h *= 1099511628211ULL; }
    snprintf(out, 17, "%016llx", h);
}

/* The on-disk format version (INDEX/version). 0 if the file is absent (a legacy
 * index predating versioning). Returns the version, or -1 on error. */
static long store_read_version(const ais *a)
{
    char path[AIS_PATH_MAX];
    char buf[32];
    FILE *fp;
    long v = 0;

    if (store_path(a, "version", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "r");
    if (fp == NULL)
        return (errno == ENOENT) ? 0 : -1;
    if (fgets(buf, sizeof(buf), fp) != NULL)
        v = atol(buf);
    fclose(fp);
    return v;
}

int store_write_version(const ais *a)
{
    char path[AIS_PATH_MAX];
    FILE *fp;

    if (store_path(a, "version", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "w");
    if (fp == NULL)
        return -1;
    fprintf(fp, "%d\n", AIS_FORMAT_VERSION);
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

/* (Re)load next_id from disk, recovering it from the store (max id + 1) if the
 * cache file is absent. Called at open, and again by every writer under the
 * exclusive lock, so two processes never hand out the same id. Returns 0/-1. */
int store_load_next_id(ais *a)
{
    char nidpath[AIS_PATH_MAX];
    FILE *fp;
    long cached = 0;

    a->next_id = 1;
    if (store_path(a, "next_id", nidpath, sizeof(nidpath)) != 0)
        return -1;
    fp = fopen(nidpath, "r");
    if (fp != NULL) {
        char buf[64];
        if (fgets(buf, sizeof(buf), fp) != NULL)
            cached = atol(buf);
        fclose(fp);
    }
    if (cached > 0) {                       /* good cache: the O(1) fast path */
        a->next_id = cached;
        return 0;
    }
    /* The cache is absent OR present-but-unusable (0-length / unparseable -- a
     * write interrupted by a crash or ENOSPC leaves it so). Trusting it here
     * would reset ids to 1 and reissue live ids, colliding records silently, so
     * fall back to the store, which is the source of truth: max(id)+1 is never
     * below any id that was durably assigned (store_append predates the cache
     * write). */
    {
        long nid = store_recover_next_id(a);
        if (nid < 0)
            return -1;
        a->next_id = nid;
    }
    return 0;
}

int store_open(ais *a, const char *dir)
{
    char lockpath[AIS_PATH_MAX];
    int n;

    a->lock_fd = -1;
    a->next_id = 1;

    n = snprintf(a->dir, sizeof(a->dir), "%s", dir);
    if (n < 0 || (size_t)n >= sizeof(a->dir))
        return -1;

    if (mkdir(a->dir, 0777) != 0 && errno != EEXIST)
        return -1;

    /* Open the lock file but DO NOT lock here. Reads take no lock (Unix: any
     * number of readers run at once); writers take the exclusive lock per
     * mutating op (store_wlock). So a long-lived reader such as `ais serve`
     * never blocks the CLI or an agent. */
    if (store_path(a, "lock", lockpath, sizeof(lockpath)) != 0)
        return -1;
    a->lock_fd = open(lockpath, O_CREAT | O_RDWR, 0666);
    if (a->lock_fd < 0)
        return -1;

    if (store_load_next_id(a) != 0)
        goto fail;

    /* format version: stamp a new or legacy index; refuse a future format
     * rather than risk misreading an index a newer ais wrote. */
    {
        long v = store_read_version(a);
        if (v < 0)
            goto fail;
        if (v > AIS_FORMAT_VERSION) {
            fprintf(stderr, "ais: index format v%ld is newer than this ais "
                            "(format v%d); upgrade ais\n", v, AIS_FORMAT_VERSION);
            goto fail;
        }
        /* Stamp a new (v0) index, and upgrade an older one in place: once this
         * ais writes a v2 line into it, a v1 ais must no longer open it (it
         * would misread the ts as keys), so mark it ours now. v2 reads v1 lines
         * fine, so the upgrade is safe and one-way. */
        if (v < AIS_FORMAT_VERSION && store_write_version(a) != 0)
            goto fail;
    }
    return 0;

fail:
    close(a->lock_fd);
    a->lock_fd = -1;
    return -1;
}

/* Take the exclusive writer lock for one mutating op (blocking: a second writer
 * waits rather than failing). The caller reloads next_id after this if it will
 * assign ids. Returns 0/-1. */
int store_wlock(ais *a)
{
    if (a->lock_fd < 0)
        return -1;
    return flock(a->lock_fd, LOCK_EX) == 0 ? 0 : -1;
}

void store_wunlock(ais *a)
{
    if (a->lock_fd >= 0)
        flock(a->lock_fd, LOCK_UN);
}

void store_close(ais *a)
{
    /* No lock is held between ops, and next_id is persisted by each write under
     * the lock; re-saving a possibly-stale in-memory next_id here would clobber
     * a concurrent writer, so close only releases the fd. */
    if (a->lock_fd >= 0) {
        close(a->lock_fd);
        a->lock_fd = -1;
    }
}

int store_save_next_id(const ais *a)
{
    char path[AIS_PATH_MAX];
    FILE *fp;

    if (store_path(a, "next_id", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "w");
    if (fp == NULL)
        return -1;
    fprintf(fp, "%ld\n", a->next_id);
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

int store_append(const ais *a, long id, const char *ts,
                 const char *keys, const char *value)
{
    char path[AIS_PATH_MAX];
    FILE *fp;
    int need;

    /* A record is ONE line: an embedded newline would end the fgets on read and
     * drop everything after it (silent, unrecoverable data loss). Keys are already
     * '|'/control-sanitized upstream; refuse a multi-line value here and point at
     * --doc, mirroring the too-long guard below. */
    if (strpbrk(value, "\r\n") != NULL || strpbrk(keys, "\r\n") != NULL) {
        fprintf(stderr, "ais: value spans multiple lines -- use --doc for multi-line/large values\n");
        return -1;
    }

    /* The whole record must round-trip through one AIS_LINE_MAX fgets on read; refuse one
     * that would not (large content belongs in a --doc blob, not inline). */
    need = (ts != NULL && ts[0] != '\0')
         ? snprintf(NULL, 0, "%ld|%s|%s|%s\n", id, ts, keys, value)
         : snprintf(NULL, 0, "%ld|%s|%s\n", id, keys, value);
    if (need < 0 || need >= AIS_LINE_MAX) {
        fprintf(stderr, "ais: record too long (%d bytes; max %d) -- use --doc for large values\n",
                need, AIS_LINE_MAX - 1);
        return -1;
    }

    if (store_path(a, "store", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "a");
    if (fp == NULL)
        return -1;
    if (ts != NULL && ts[0] != '\0')
        fprintf(fp, "%ld|%s|%s|%s\n", id, ts, keys, value);   /* v2 */
    else
        fprintf(fp, "%ld|%s|%s\n", id, keys, value);          /* legacy */
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

int store_find_value(const ais *a, const char *value, long *out_id)
{
    char path[AIS_PATH_MAX];
    char line[AIS_LINE_MAX];
    FILE *fp;
    int rc = 0;

    if (store_path(a, "store", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "r");
    if (fp == NULL)
        return (errno == ENOENT) ? 0 : -1;   /* no store yet -> not found */

    while (fgets(line, sizeof(line), fp) != NULL) {
        long id;
        char *ts, *keys, *val;
        if (store_parse(line, &id, &ts, &keys, &val) != 0)
            continue;
        if (strcmp(val, value) == 0) {
            *out_id = id;
            rc = 1;
            break;
        }
    }
    fclose(fp);
    return rc;
}

int store_each_record(const ais *a, store_rec_cb cb, void *ctx)
{
    char path[AIS_PATH_MAX];
    char line[AIS_LINE_MAX];
    FILE *fp;
    int rc = 0;

    if (store_path(a, "store", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "r");
    if (fp == NULL)
        return (errno == ENOENT) ? 0 : -1;

    while (fgets(line, sizeof(line), fp) != NULL) {
        long id;
        char *ts, *keys, *val;
        if (store_parse(line, &id, &ts, &keys, &val) != 0)
            continue;
        rc = cb(id, ts, keys, val, ctx);
        if (rc != 0)
            break;
    }
    fclose(fp);
    return rc;
}

long store_recover_next_id(const ais *a)
{
    char path[AIS_PATH_MAX];
    char line[AIS_LINE_MAX];
    FILE *fp;
    long maxid = 0;

    if (store_path(a, "store", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "r");
    if (fp == NULL)
        return (errno == ENOENT) ? 1 : -1;   /* empty store -> first id is 1 */

    while (fgets(line, sizeof(line), fp) != NULL) {
        long id;
        char *ts, *keys, *val;
        if (store_parse(line, &id, &ts, &keys, &val) != 0)
            continue;
        if (id > maxid)
            maxid = id;
    }
    fclose(fp);
    return maxid + 1;
}

/* --- record fast path: "off" (id->offset) and "multi" (multi-line ids) ----- */

long store_bytes(const ais *a)
{
    char path[AIS_PATH_MAX];
    struct stat st;

    if (store_path(a, "store", path, sizeof(path)) != 0)
        return -1;
    if (stat(path, &st) != 0)
        return (errno == ENOENT) ? 0 : -1;   /* no store yet -> offset 0 */
    return (long)st.st_size;
}

void off_write(FILE *fp, long offset)
{
    long v = (offset < 0) ? 0L : offset + 1;   /* +1: 0 = absent */
    if (v >= 100000000000L)                    /* 11 digits (~90 GB); a larger value would
                                                * print 12 digits and break the fixed
                                                * AIS_OFF_WIDTH stride, misaligning every later
                                                * entry. Emit the absent sentinel instead so
                                                * off_get falls back to a scan (still correct),
                                                * rather than silently returning wrong offsets. */
        v = 0;
    fprintf(fp, "%011ld\n", v);
}

int off_append(const ais *a, long offset)
{
    char path[AIS_PATH_MAX];
    FILE *fp;

    if (store_path(a, "off", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "a");
    if (fp == NULL)
        return -1;
    off_write(fp, offset);
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

int off_consistent(const ais *a)
{
    char path[AIS_PATH_MAX];
    struct stat st;
    long want = (a->next_id - 1) * (long)AIS_OFF_WIDTH;

    if (store_path(a, "off", path, sizeof(path)) != 0)
        return 0;
    if (stat(path, &st) != 0)
        return (errno == ENOENT && want == 0) ? 1 : 0;   /* fresh index: ok */
    return (st.st_size == want) ? 1 : 0;
}

int off_get(const ais *a, long id, long *offset)
{
    char path[AIS_PATH_MAX];
    char buf[AIS_OFF_WIDTH + 1];
    FILE *fp;
    long v;

    if (id <= 0)
        return 0;
    if (store_path(a, "off", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "r");
    if (fp == NULL)
        return (errno == ENOENT) ? 0 : -1;   /* no off -> caller scans */
    if (fseek(fp, (long)(id - 1) * AIS_OFF_WIDTH, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    if (fread(buf, 1, AIS_OFF_WIDTH, fp) != (size_t)AIS_OFF_WIDTH) {
        fclose(fp);
        return 0;                            /* short: id beyond the index */
    }
    fclose(fp);
    buf[AIS_OFF_WIDTH] = '\0';
    v = atol(buf);
    if (v == 0)
        return 0;                            /* sentinel: absent (a gap) */
    *offset = v - 1;
    return 1;
}

int store_value_at(const ais *a, long id, long offset, ais_val_cb cb, void *ctx)
{
    char path[AIS_PATH_MAX];
    char line[AIS_LINE_MAX];
    FILE *fp;
    long lid;
    char *ts, *keys, *val;

    if (store_path(a, "store", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "r");
    if (fp == NULL)
        return -1;
    if (fseek(fp, offset, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    if (store_parse(line, &lid, &ts, &keys, &val) != 0)
        return 0;
    if (lid != id)
        return 0;                            /* offset stale: caller scans */
    (void)ts;
    (void)keys;
    cb(id, val, ctx);
    return 1;
}

/* Like store_value_at, but parses the WHOLE record (id|ts|keys|value) at OFFSET
 * and forwards it to a store_rec_cb -- so a caller paging by id (the timeline)
 * can read one record without scanning. 1 served, 0 stale/mismatch, -1 error. */
int store_record_at(const ais *a, long id, long offset, store_rec_cb cb, void *ctx)
{
    char path[AIS_PATH_MAX];
    char line[AIS_LINE_MAX];
    FILE *fp;
    long lid;
    char *ts, *keys, *val;

    if (store_path(a, "store", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "r");
    if (fp == NULL)
        return -1;
    if (fseek(fp, offset, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    if (store_parse(line, &lid, &ts, &keys, &val) != 0)
        return 0;
    if (lid != id)
        return 0;                            /* offset stale */
    cb(id, ts, keys, val, ctx);
    return 1;
}

int multi_append(const ais *a, long id)
{
    char path[AIS_PATH_MAX];
    FILE *fp;

    if (store_path(a, "multi", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "a");
    if (fp == NULL)
        return -1;
    fprintf(fp, "%ld\n", id);
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

int multi_contains(const ais *a, long id)
{
    char path[AIS_PATH_MAX];
    char line[64];
    FILE *fp;
    int found = 0;

    if (store_path(a, "multi", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "r");
    if (fp == NULL)
        return (errno == ENOENT) ? 0 : -1;   /* no multi -> none are multi */
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (atol(line) == id) {
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found;
}
