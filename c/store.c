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

/* Does field-2 of a store line hold a timestamp? Tells a v2 line
 * (id|ts|keys|value) from a legacy v1 line (id|keys|value). Accepts the engine's
 * own "YYYY-MM-DDThh:mm:ss" and the hand-written "YYYY-MM-DD" and
 * "YYYY-MM-DDThh:mm", always anchored and ending at '|'/EOL. The lower bound is a
 * FULL date: a bare year or "YYYY-MM" stays a KEY, tagging by year ("photos
 * 2026") being common. A malformed date fails here and the line reads as a
 * dateless v1 record -- only the date is dropped, never the id, keys or value. */
/* One second later, on the canonical "YYYY-MM-DDThh:mm:ssZ" form. Civil
 * arithmetic, not timegm()/mktime() (non-standard and local-time respectively);
 * the string is already UTC. Returns 0, or -1 if TS is not that exact form. */
static int atoi_n(const char *p, int off, int n)
{
    int v = 0;
    while (n-- > 0)
        v = v * 10 + (p[off++] - '0');
    return v;
}

int store_ts_next_second(const char *ts, char *out, size_t outsz)
{
    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int y, mo, d, h, mi, se, last;

    if (ts == NULL || strlen(ts) != 20 || !store_looks_like_ts(ts))
        return -1;
    y  = atoi_n(ts, 0, 4);  mo = atoi_n(ts, 5, 2);  d  = atoi_n(ts, 8, 2);
    h  = atoi_n(ts, 11, 2); mi = atoi_n(ts, 14, 2); se = atoi_n(ts, 17, 2);
    if (mo < 1 || mo > 12 || d < 1)
        return -1;

    if (++se > 59) { se = 0; if (++mi > 59) { mi = 0; if (++h > 23) { h = 0;
        last = mdays[mo - 1];
        if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
            last = 29;
        if (++d > last) { d = 1; if (++mo > 12) { mo = 1; y++; } }
    } } }
    return (snprintf(out, outsz, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                     y, mo, d, h, mi, se) == 20) ? 0 : -1;
}

int store_looks_like_ts(const char *p)
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
    if (store_looks_like_ts(p)) {
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
 * dedups by value (keys are labels that union on merge). FNV-1a 64-bit as 16 hex
 * chars + NUL. NOT a security hash. Same value -> same hash on any device. */
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
 * cache file is absent. Called at open, and by every writer under the exclusive
 * lock, so two processes never hand out the same id. Returns 0/-1. */
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
    /* Cache absent, or present but unusable (0-length / unparseable, as a write
     * interrupted by a crash or ENOSPC leaves it). Trusting it would reset ids to
     * 1 and reissue live ones, colliding records silently. The store is the source
     * of truth: max(id)+1 is never below any durably assigned id, store_append
     * preceding the cache write. */
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
    a->purge_deletes = 0;
    a->next_id = 1;
    a->survivals = 0;

    n = snprintf(a->dir, sizeof(a->dir), "%s", dir);
    if (n < 0 || (size_t)n >= sizeof(a->dir))
        return -1;

    if (mkdir(a->dir, 0777) != 0 && errno != EEXIST)
        return -1;

    /* Open the lock file but DO NOT lock here: reads take no lock, writers take
     * the exclusive lock per mutating op (store_wlock). A long-lived reader such
     * as `ais serve` never blocks the CLI or an agent. */
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
            /* Reached by a user whose devices share one index folder and who has
             * upgraded only some of them: the index is intact, this ais is the
             * old part, and saying so stops a "fix" by deleting files. */
            fprintf(stderr,
                    "ais: this index was written by a newer ais (format v%ld; "
                    "this one speaks v%d).\n"
                    "     Your data is fine and nothing has been changed. Update ais "
                    "on this device\n"
                    "     to open it -- opening it with this version could undo edits "
                    "made on the others.\n", v, AIS_FORMAT_VERSION);
            goto fail;
        }
        /* Stamp a new (v0) index and upgrade an older one in place: once this ais
         * writes a v2 line into it, a v1 ais must not open it (it would misread
         * the ts as keys). v2 reads v1 lines, so the upgrade is safe and one-way. */
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
 * waits rather than failing). A caller that will assign ids reloads next_id
 * after this. Returns 0/-1. */
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
    /* No lock is held between ops and each write persists next_id under the lock;
     * re-saving a possibly-stale in-memory next_id here would clobber a concurrent
     * writer, so close only releases the fd. */
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
                 const char *keys, const char *value, long *start)
{
    char path[AIS_PATH_MAX];
    FILE *fp;
    int need;

    /* A record is ONE line: an embedded newline ends the fgets on read and drops
     * everything after it (silent, unrecoverable loss). Keys are already
     * '|'/control-sanitized upstream; a multi-line value is refused here. */
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
    fp = fopen(path, "a+");
    if (fp == NULL)
        return -1;

    /* NEVER append onto an unterminated last line. A record can be 64 KB
     * (AIS_LINE_MAX) against stdio's 4 KB buffer, so one append is several
     * write() calls and a power cut or ENOSPC between them leaves a partial line.
     * Appending onto it fuses the two records into one line: store_parse takes
     * the whole following record as the value, and the next compaction rewrites
     * the fused line verbatim. Two records lost, silently.
     *
     * The newline is added rather than the tail truncated: a store is meant to be
     * hand-editable and an editor leaving no final newline is ordinary, so that
     * last line can be a COMPLETE record. A genuinely torn one is then damaged
     * alone (STYLE.md: "corruption must stay local and recoverable"). No fsync:
     * a disk flush per save to narrow a window this already makes survivable;
     * compact.c's rename commit takes the same view. */
    if (fseek(fp, 0, SEEK_END) == 0) {
        long sz = ftell(fp);
        if (sz > 0 && fseek(fp, sz - 1, SEEK_SET) == 0) {
            int last = fgetc(fp);
            if (fseek(fp, 0, SEEK_END) == 0 && last != EOF && last != '\n')
                fputc('\n', fp);
        }
        if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    }

    /* AFTER the tail is closed: the size beforehand is off by one whenever the
     * healing newline was written, and an "off" entry built from it points at
     * that newline instead of the record. -1 (seek/tell failure) becomes the
     * absent sentinel downstream, which only costs the reader a fallback scan. */
    if (start != NULL)
        *start = (fseek(fp, 0, SEEK_END) == 0) ? ftell(fp) : -1;

    if (ts != NULL && ts[0] != '\0')
        fprintf(fp, "%ld|%s|%s|%s\n", id, ts, keys, value);   /* v2 */
    else
        fprintf(fp, "%ld|%s|%s\n", id, keys, value);          /* legacy */
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

int store_read_line(char *buf, size_t sz, FILE *fp)
{
    int c;

    if (fgets(buf, sz, fp) == NULL)
        return 0;
    if (strchr(buf, '\n') != NULL)
        return 1;
    if (feof(fp))                    /* a final line with no trailing newline */
        return 1;
    while ((c = fgetc(fp)) != EOF && c != '\n')   /* drop the oversized remainder */
        ;
    return -1;
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

void off_write(FILE *fp, long offset)
{
    long v = (offset < 0) ? 0L : offset + 1;   /* +1: 0 = absent */
    if (v >= 100000000000L)                    /* 11 digits (~90 GB); 12 would break the
                                                * fixed AIS_OFF_WIDTH stride and misalign
                                                * every later entry. Emit the absent
                                                * sentinel: off_get then falls back to a
                                                * scan instead of returning wrong offsets. */
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
 * and forwards it to a store_rec_cb, so a caller paging by id (the timeline) can
 * read one record without scanning. 1 served, 0 stale/mismatch, -1 error. */
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
