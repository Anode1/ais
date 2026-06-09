/* stats.c -- index statistics. See stats.h.
 *
 * Streaming throughout: store and tomb are read line by line into one fixed
 * buffer, the idx/ tree is walked with opendir/readdir. Peak footprint is the
 * struct/buffer sizes, never the data (STYLE.md: bounded memory).
 */
#define _DEFAULT_SOURCE      /* dirent */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "common.h"
#include "stats.h"

/* Parse the leading id of a store/tomb line ("id|..." or "id\n").
 * Returns the id (> 0), or 0 if the line has no parseable id. */
static long stats_line_id(const char *line)
{
    char *end;
    long id;

    errno = 0;
    id = strtol(line, &end, 10);
    if (end == line || id <= 0)
        return 0;   /* blank / non-numeric line: ignore it */
    return id;
}

/* Count distinct ids in "<dir>/<name>", a file of "id|...." lines whose ids
 * arrive in nondecreasing order (the store is physically id-ordered; tomb is
 * appended in del order, also nondecreasing). Distinct ids are therefore
 * consecutive, so deduping needs only the previous id -- bounded memory.
 * A missing file counts as 0. Returns 0 on success, -1 on error. */
static int stats_count_ids(const ais *a, const char *name, long *out)
{
    char path[AIS_PATH_MAX];
    char line[AIS_LINE_MAX];
    FILE *fp;
    long count = 0, prev = 0;
    int n, rc = 0;

    *out = 0;
    n = snprintf(path, sizeof(path), "%s/%s", a->dir, name);
    if (n < 0 || (size_t)n >= sizeof(path))
        return -1;

    fp = fopen(path, "r");
    if (fp == NULL)
        return (errno == ENOENT) ? 0 : -1;   /* absent -> 0 */

    while (fgets(line, sizeof(line), fp) != NULL) {
        long id = stats_line_id(line);
        if (id == 0 || id == prev)
            continue;   /* skip blanks and continuation lines of the same id */
        count++;
        prev = id;
    }
    if (ferror(fp))
        rc = -1;
    if (fclose(fp) != 0)
        rc = -1;
    if (rc == 0)
        *out = count;
    return rc;
}

/* Count distinct LIVE record ids: store ids not present in tomb. Both files
 * are id-ordered, so this is a streaming merge keeping only one id per file --
 * memory is O(1). A missing store counts as 0; a missing tomb suppresses
 * nothing. Returns 0 on success, -1 on error. */
static int stats_count_live(const ais *a, long *out)
{
    char spath[AIS_PATH_MAX], tpath[AIS_PATH_MAX];
    char line[AIS_LINE_MAX];
    char tline[AIS_LINE_MAX];
    FILE *sf = NULL, *tf = NULL;
    long count = 0, prev = 0, tid = 0;
    int sn, tn, rc = 0;

    *out = 0;
    sn = snprintf(spath, sizeof(spath), "%s/store", a->dir);
    tn = snprintf(tpath, sizeof(tpath), "%s/tomb", a->dir);
    if (sn < 0 || (size_t)sn >= sizeof(spath) ||
        tn < 0 || (size_t)tn >= sizeof(tpath))
        return -1;

    sf = fopen(spath, "r");
    if (sf == NULL)
        return (errno == ENOENT) ? 0 : -1;   /* no store -> 0 live records */

    tf = fopen(tpath, "r");   /* may be NULL: no tombstones */
    if (tf != NULL) {         /* prime the tomb head */
        if (fgets(tline, sizeof(tline), tf) != NULL)
            tid = stats_line_id(tline);
    }

    while (fgets(line, sizeof(line), sf) != NULL) {
        long id = stats_line_id(line);
        if (id == 0 || id == prev)
            continue;   /* blank or another link line of the current id */
        prev = id;

        /* advance the tomb head to >= id (tomb is id-ordered) */
        while (tf != NULL && tid != 0 && tid < id) {
            if (fgets(tline, sizeof(tline), tf) != NULL)
                tid = stats_line_id(tline);
            else
                tid = 0;
        }
        if (tf != NULL && tid == id)
            continue;   /* tombstoned: not live */
        count++;
    }
    if (ferror(sf))
        rc = -1;
    if (tf != NULL && ferror(tf))
        rc = -1;

    if (fclose(sf) != 0)
        rc = -1;
    if (tf != NULL && fclose(tf) != 0)
        rc = -1;

    if (rc == 0)
        *out = count;
    return rc;
}

/* Count key files under idx/<p>/ by walking each prefix dir. Non-dir or
 * unreadable prefix entries are skipped (corruption stays local). A missing
 * idx/ counts as 0. Returns 0 on success, -1 on error. */
static int stats_count_keys(const ais *a, long *out)
{
    char idxdir[AIS_PATH_MAX];
    DIR *idx = NULL;
    struct dirent *pe;
    long count = 0;
    int rc = 0;

    *out = 0;
    if (snprintf(idxdir, sizeof(idxdir), "%s/idx", a->dir) >= (int)sizeof(idxdir))
        return -1;

    idx = opendir(idxdir);
    if (idx == NULL)
        return (errno == ENOENT) ? 0 : -1;   /* no idx/ -> 0 keys */

    while ((pe = readdir(idx)) != NULL) {
        char pdir[AIS_PATH_MAX];
        DIR *pd;
        struct dirent *ke;

        if (pe->d_name[0] == '.')   /* skip "." and ".." */
            continue;
        if (snprintf(pdir, sizeof(pdir), "%s/%s", idxdir, pe->d_name)
                >= (int)sizeof(pdir))
            continue;
        pd = opendir(pdir);
        if (pd == NULL)
            continue;   /* not a dir (or unreadable): stay local, skip */
        while ((ke = readdir(pd)) != NULL) {
            if (ke->d_name[0] == '.')
                continue;
            count++;
        }
        closedir(pd);
    }
    closedir(idx);

    *out = count;
    return rc;
}

int ais_stats(ais *a, FILE *out)
{
    long records = 0, keys = 0, deleted = 0;

    if (stats_count_live(a, &records) != 0)
        return -1;
    if (stats_count_keys(a, &keys) != 0)
        return -1;
    if (stats_count_ids(a, "tomb", &deleted) != 0)
        return -1;

    fprintf(out, "records: %ld\n", records);
    fprintf(out, "keys: %ld\n", keys);
    fprintf(out, "deleted: %ld\n", deleted);
    return 0;
}
