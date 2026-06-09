/* store.c -- append-only store, monotonic id, writer lock. See store.h. */
#define _DEFAULT_SOURCE      /* flock, mkdir, fdopen via BSD/POSIX */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "store.h"

/* Build "<dir>/<name>" into OUT. Returns 0, or -1 if it would not fit. */
static int store_path(const ais *a, const char *name, char *out, size_t outsz)
{
    int n = snprintf(out, outsz, "%s/%s", a->dir, name);
    if (n < 0 || (size_t)n >= outsz)
        return -1;
    return 0;
}

/* Split a store line "id|keys|value" in place. Trims the trailing newline.
 * On success sets *id, *keys, *value (pointers into LINE) and returns 0.
 * Returns -1 if the line is malformed (no id field, etc.). */
static int store_parse(char *line, long *id, char **keys, char **value)
{
    char *bar1, *bar2;
    size_t len = strlen(line);

    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';

    bar1 = strchr(line, '|');
    if (bar1 == NULL)
        return -1;
    *bar1 = '\0';
    *id = atol(line);
    if (*id <= 0)
        return -1;
    *keys = bar1 + 1;
    bar2 = strchr(*keys, '|');
    if (bar2 == NULL) {
        *value = *keys + strlen(*keys);   /* empty value */
    } else {
        *bar2 = '\0';
        *value = bar2 + 1;
    }
    return 0;
}

int store_open(ais *a, const char *dir)
{
    char lockpath[AIS_PATH_MAX];
    int n;
    long nid;

    a->lock_fd = -1;
    a->next_id = 1;

    n = snprintf(a->dir, sizeof(a->dir), "%s", dir);
    if (n < 0 || (size_t)n >= sizeof(a->dir))
        return -1;

    if (mkdir(a->dir, 0777) != 0 && errno != EEXIST)
        return -1;

    if (store_path(a, "lock", lockpath, sizeof(lockpath)) != 0)
        return -1;
    a->lock_fd = open(lockpath, O_CREAT | O_RDWR, 0666);
    if (a->lock_fd < 0)
        return -1;
    if (flock(a->lock_fd, LOCK_EX | LOCK_NB) != 0) {
        close(a->lock_fd);
        a->lock_fd = -1;
        return -1;
    }

    /* load next_id, or recover from the store if the file is absent */
    {
        char nidpath[AIS_PATH_MAX];
        FILE *fp;

        if (store_path(a, "next_id", nidpath, sizeof(nidpath)) != 0)
            goto fail;
        fp = fopen(nidpath, "r");
        if (fp != NULL) {
            char buf[64];
            if (fgets(buf, sizeof(buf), fp) != NULL && atol(buf) > 0)
                a->next_id = atol(buf);
            fclose(fp);
        } else {
            nid = store_recover_next_id(a);
            if (nid < 0)
                goto fail;
            a->next_id = nid;
        }
    }
    return 0;

fail:
    flock(a->lock_fd, LOCK_UN);
    close(a->lock_fd);
    a->lock_fd = -1;
    return -1;
}

void store_close(ais *a)
{
    if (a->lock_fd >= 0) {
        store_save_next_id(a);
        flock(a->lock_fd, LOCK_UN);
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

int store_append(const ais *a, long id, const char *keys, const char *value)
{
    char path[AIS_PATH_MAX];
    FILE *fp;

    if (store_path(a, "store", path, sizeof(path)) != 0)
        return -1;
    fp = fopen(path, "a");
    if (fp == NULL)
        return -1;
    fprintf(fp, "%ld|%s|%s\n", id, keys, value);
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
        char *keys, *val;
        if (store_parse(line, &id, &keys, &val) != 0)
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
        char *keys, *val;
        if (store_parse(line, &id, &keys, &val) != 0)
            continue;
        rc = cb(id, keys, val, ctx);
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
        char *keys, *val;
        if (store_parse(line, &id, &keys, &val) != 0)
            continue;
        if (id > maxid)
            maxid = id;
    }
    fclose(fp);
    return maxid + 1;
}
