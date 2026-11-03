/* due.c -- reminders as keys: which "due:DATE" keys have elapsed, and the live
 * records filed under them. The contract is ais_due() in ais.h.
 *
 * Nothing here is stored: a due key is an ordinary key in an ordinary posting
 * list, read through ais_get like any recall. This file only decides which keys
 * are elapsed on a given day and turns the matching ids into timeline rows.
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ais.h"
#include "common.h"
#include "due.h"
#include "key.h"       /* key_fsdecode: a shard lists file names, not keys */
#include "compact.h"   /* tomb_contains: a posting may still list deleted ids */
#include "post.h"      /* the key's id stream, to see whether anything is live */

#define DUE_PREFIX     "due:"
#define DUE_PREFIX_LEN 4
#define DUE_DATE_MAX   10              /* "YYYY-MM-DD" */

/* The date part of an encoded due key, if it has the shape YYYY, YYYY-MM or
 * YYYY-MM-DD: digits where the store's ts has digits, '-' where it has '-'.
 * Returns its length, or 0 when NAME is not a due key of that shape. */
static int due_date_len(const char *name)
{
    static const char shape[] = "dddd-dd-dd";
    const char *d;
    size_t n, i;

    if (strncmp(name, DUE_PREFIX, DUE_PREFIX_LEN) != 0)
        return 0;
    d = name + DUE_PREFIX_LEN;
    n = strlen(d);
    if (n != 4 && n != 7 && n != 10)
        return 0;
    for (i = 0; i < n; i++) {
        if (shape[i] == 'd' ? !(d[i] >= '0' && d[i] <= '9') : d[i] != '-')
            return 0;
    }
    return (int)n;
}

int due_key_check(const char *key)
{
    static const char shape[] = "dddd-dd-dd";
    size_t n, i;
    int mm, dd;

    if (key == NULL)
        return -1;
    for (i = 0; i < DUE_PREFIX_LEN; i++) {
        char c = key[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c != DUE_PREFIX[i])
            return -1;
    }
    key += DUE_PREFIX_LEN;
    n = strlen(key);
    if (n != 4 && n != 7 && n != 10)
        return 0;
    for (i = 0; i < n; i++)
        if (shape[i] == 'd' ? !(key[i] >= '0' && key[i] <= '9') : key[i] != '-')
            return 0;
    if (n >= 7) {
        mm = (key[5] - '0') * 10 + (key[6] - '0');
        if (mm < 1 || mm > 12)
            return 0;
    }
    if (n == 10) {
        dd = (key[8] - '0') * 10 + (key[9] - '0');
        if (dd < 1 || dd > 31)
            return 0;
    }
    return 1;
}

/* Today as "YYYY-MM-DD" in local time. */
static void due_today(char *out, size_t outsz)
{
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);

    if (lt == NULL || strftime(out, outsz, "%Y-%m-%d", lt) == 0)
        snprintf(out, outsz, "0000-00-00");   /* no clock: nothing elapses */
}

/* The elapsed keys found so far, kept sorted ascending so the set is the
 * lexically smallest when the cap is hit. Dates only: the prefix is implied. */
struct due_set {
    char dates[AIS_KEYS_MAX][DUE_DATE_MAX + 1];
    int  n;
};

static void due_set_add(struct due_set *s, const char *date)
{
    int i, j;

    for (i = 0; i < s->n; i++) {
        int c = strcmp(date, s->dates[i]);
        if (c == 0)
            return;
        if (c < 0)
            break;
    }
    if (i == s->n) {
        if (s->n == AIS_KEYS_MAX)
            return;                     /* larger than every kept date: drop */
    } else if (s->n == AIS_KEYS_MAX) {
        s->n--;                         /* full: the largest makes room */
    }
    for (j = s->n; j > i; j--)
        memcpy(s->dates[j], s->dates[j - 1], sizeof s->dates[j]);
    snprintf(s->dates[i], sizeof s->dates[i], "%s", date);
    s->n++;
}

/* A key with no live record: every id in its posting is tombstoned. A deleted
 * record keeps its posting line until compaction, and sixty-four dead keys
 * would otherwise fill the set and hide a live reminder behind them. Stops at
 * the first live id, so a live key costs one read. */
static int due_key_dead(const ais *a, const char *key)
{
    post_stream s;
    int dead = 1;

    if (post_open(a, key, &s) != 0)
        return 1;
    for (; s.alive; post_next(&s))
        if (tomb_contains(a, s.head) == 0) {
            dead = 0;
            break;
        }
    post_close(&s);
    return dead;
}

/* ais_get hands each surviving id; the row for it is the newest live timeline
 * row below id + 1, which is the id itself. ais_timeline does not return the
 * callback's code, so it is kept here and handed back to ais_get, which stops
 * on it. */
struct due_rows { ais *a; ais_tl_cb cb; void *ctx; int rc; };

static int due_emit(long id, const char *ts, const char *keys, const char *value,
                    void *vp)
{
    struct due_rows *r = vp;
    r->rc = r->cb(id, ts, keys, value, r->ctx);
    return r->rc;
}

static int due_row(long id, void *vp)
{
    struct due_rows *r = vp;
    if (ais_timeline(r->a, id + 1, 1, "", "", due_emit, r) < 0)
        return -1;
    return r->rc;
}

int ais_due(ais *a, const char *today, ais_tl_cb cb, void *ctx)
{
    char day[DUE_DATE_MAX + 1];
    char shard[AIS_PATH_MAX];
    char keys[AIS_KEYS_MAX][DUE_PREFIX_LEN + DUE_DATE_MAX + 1];
    char *keyp[AIS_KEYS_MAX];
    struct due_set set;
    struct due_rows rows;
    DIR *d;
    struct dirent *e;
    int i;

    if (a == NULL || cb == NULL)
        return -1;
    if (today != NULL && today[0] != '\0') {
        if (strlen(today) != DUE_DATE_MAX)
            return -1;                  /* a timestamp is not a day */
        snprintf(day, sizeof day, "%s", today);
    } else {
        due_today(day, sizeof day);
    }

    /* "due:" encodes to itself, so its shard is the first two characters. */
    if (snprintf(shard, sizeof shard, "%s/idx/du", a->dir) >= (int)sizeof shard)
        return -1;
    d = opendir(shard);
    if (d == NULL)
        return 0;                       /* no shard, or no idx/ yet: nothing due */
    set.n = 0;
    while ((e = readdir(d)) != NULL) {
        char key[AIS_KEY_MAX];      /* the file name is not the key on Windows */
        int n;
        if (key_fsdecode(e->d_name, key, sizeof key) != 0)
            continue;
        n = due_date_len(key);
        /* elapsed: the key's date, at its own precision, is not after today */
        if (n > 0 && strncmp(key + DUE_PREFIX_LEN, day, (size_t)n) <= 0
            && !due_key_dead(a, key))
            due_set_add(&set, key + DUE_PREFIX_LEN);
    }
    closedir(d);
    if (set.n == 0)
        return 0;

    for (i = 0; i < set.n; i++) {
        snprintf(keys[i], sizeof keys[i], "%s%s", DUE_PREFIX, set.dates[i]);
        keyp[i] = keys[i];
    }
    rows.a = a; rows.cb = cb; rows.ctx = ctx; rows.rc = 0;
    return ais_get(a, keyp, set.n, AIS_OR, due_row, &rows);
}
