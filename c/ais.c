/* ais.c -- the public facade. Composes store/post/merge/compact into the
 * ais.h API. Modules return 0/-1; this layer does the same (only main.c dies).
 */
#define _DEFAULT_SOURCE      /* strtok_r */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ais.h"
#include "compact.h"
#include "key.h"          /* key_encode: keys_union dedups on the posting's identity */
#include "log.h"
#include "merge.h"
#include "post.h"
#include "store.h"
#include "win.h"          /* rename() -> MoveFileEx shim on native Windows; empty on POSIX */

const char *ais_version(void)
{
    return AIS_VERSION;
}

long ais_version_number(void)
{
    const char *v = AIS_VERSION;
    long part[3] = { 0, 0, 0 };
    int i = 0;

    /* "0.3.9", or what git describe adds: "0.3.9-2-gabc1234", "0.3.9-dirty".
     * Stop at the first non-digit, non-dot -- the suffix is not ordering. */
    for (; *v != '\0' && i < 3; v++) {
        if (*v >= '0' && *v <= '9') {
            part[i] = part[i] * 10 + (*v - '0');
            continue;
        }
        if (*v == '.') { i++; continue; }
        break;                            /* a tag that is not a plain version */
    }
    return part[0] * 1000000 + part[1] * 1000 + part[2];
}

int ais_open(ais *a, const char *dir)
{
    if (store_open(a, dir) != 0) {
        debug("cannot open index '%s' (in use, or unwritable)", dir);
        return -1;
    }
    /* A killed compaction leaves a half-built idx/ that makes every keyed lookup
     * silently under-report. Put the staged tree back before anyone reads. */
    if (compact_recover(a) < 0)
        debug("could not recover a staged idx/ in '%s'", dir);
    debug("opened index '%s', next_id=%ld", a->dir, a->next_id);
    return 0;
}

void ais_close(ais *a)
{
    store_close(a);
}

void ais_on_discard(ais *a, ais_discard_cb cb, void *ctx)
{
    if (a == NULL)
        return;
    a->discard = cb;
    a->discard_ctx = ctx;
}

/* Post each whitespace-separated key of KEYS to record ID. A bare token is
 * ATTACHED (post_insert keeps each posting ascending and duplicate-free, so
 * re-puts add nothing); a "-key" token is DETACHED -- the posting is removed and
 * the (id,key) pair recorded in ktomb, so the record keeps its id but loses the
 * key (re-attaching the same key clears the pair). Returns 0/-1. */
/* forward: stamp a record's (ts, value-hash) -- shared by delete and key-detach. */
static void del_stamp(ais *a, long id, char *ts, size_t tsz, char hash[17]);

/* Context for ais_add / the key-attach mirror: does the id exist, and its keys. */
struct add_lookup {
    long  id;
    int   found;
    char  keys[AIS_LINE_MAX];
    const char *want;          /* add_seek_dup only: the value being attached */
    char  whash[17];           /* its content_hash, computed once */
    long  dup_id;              /* another record already holding it, else 0 */
};

/* forward: the key-attach mirror into the authoritative keys field (LAYOUT.md). */
static int  add_seek(long id, const char *ts, const char *keys,
                     const char *value, void *vp);
static int  keys_union(char *keys, size_t sz, const char *tok);
static int  key_fold_stored(const char *tok, char *out, size_t sz);
static int  store_set_keys(ais *a, long id, const char *keys);
AIS_NOINLINE static int resurrect_keys(ais *a, long id, const char *want);
static int  keys_contains(const char *keys, const char *tok);
static int  store_restamp(ais *a, long id, const char *ts);
static long tag_count_file(const ais *a, const char *path, int dead);

/* Attach/detach KEYS on record ID. ATTACH_TS is what an implicit re-attach
 * LWW-competes against a prior detach with: NULL = a LOCAL edit (now, always wins,
 * clears the detach); on the merge/import path the record's add-ts, so a key
 * detached at or after it survives an unaware peer's stale A| line. */
/* Does an attach of TOK win against a prior detach? 1 = attach, 0 = leave the
 * detach standing, -1 on error. On the merge path ATTACH_TS is the record's
 * add-ts and a detach stamped at or after it wins (detaches are sticky on ties);
 * a local edit passes NULL and always wins. Shared by both passes below so the
 * store and the index can never disagree about which tokens were attached. */
static int attach_wins(ais *a, long id, const char *tok,
                       const char *attach_ts, int active)
{
    char kts[AIS_TS_MAX];
    int kt;

    if (attach_ts == NULL || !active)
        return 1;
    kt = ktomb_lookup(a, id, tok, kts, sizeof kts);
    if (kt < 0)
        return -1;
    if (kt == 1 && kts[0] != '\0' && strcmp(kts, attach_ts) >= 0)
        return 0;
    return 1;
}

/* Note WHEN a key was attached to a record that ALREADY EXISTED. The A| line
 * carries one timestamp, the record's, which cannot express a key added later, so
 * a peer that had detached that key out-ranks the attach for ever and no device in
 * the mesh can put it back. The note travels as T| (compact.h). A
 * brand-new record needs none: its keys field already says it, at its own time. */
static int katt_note(ais *a, long id, const char *tok)
{
    char kts[AIS_TS_MAX], khash[17], have[AIS_TS_MAX];
    int noted = katt_lookup(a, id, tok, have, sizeof have);

    del_stamp(a, id, kts, sizeof kts, khash);
    /* Same as att_apply: replacing costs a whole-file rewrite, and a first attach
     * -- most of them, since a key is only detached deliberately -- has nothing to
     * replace. The recorded time is the LAST attach. */
    if (noted == 1)
        return katt_set(a, id, kts, khash, tok);
    return katt_add(a, id, kts, khash, tok);
}

/* Walk KEYS token by token without copying the whole list. Fills TOK (one key
 * wide) from *P and advances it: 1 = a token, 0 = end, -1 = a token too long to
 * be a key. No line-sized copy: this sits on the primary save path's chain, which
 * has to fit the 512 KB thread stack the FFI seam runs on. */
/* Fill L (id already set) with record L->id's first line, through the "off"
 * accelerator when it can serve it and a full scan only when it cannot -- the seek
 * del_stamp uses. A whole store pass per lookup makes every key attach O(records),
 * so an import carrying an attach per tag (what a re-tagged index exports) is
 * quadratic in the store. Returns 0, or -1 on a real read error. */
static int seek_record(ais *a, struct add_lookup *L)
{
    long offset;

    L->found = 0;
    L->keys[0] = '\0';
    if (off_get(a, L->id, &offset) == 1 &&
        store_record_at(a, L->id, offset, add_seek, L) == 1 && L->found)
        return 0;
    L->found = 0;
    L->keys[0] = '\0';
    return (store_each_record(a, add_seek, L) < -1) ? -1 : 0;
}

static int keys_next(const char **p, char *tok, size_t toksz)
{
    const char *s = *p;
    size_t len;

    while (*s == ' ' || *s == '\t')
        s++;
    len = strcspn(s, " \t");
    *p = s + len;
    if (len == 0)
        return 0;
    if (len >= toksz)
        return -1;                       /* longer than any key can be */
    memcpy(tok, s, len);
    tok[len] = '\0';
    return 1;
}

static int ais_post_keys(ais *a, const char *keys, long id, const char *attach_ts,
                         int line_carries_keys)
{
    char tok[AIS_KEY_MAX];               /* one key at a time, not the whole list */
    struct add_lookup L;                 /* L.keys doubles as the union buffer */
    int  have_line_keys = 0, changed = 0;
    const char *p;
    int got, active, katt_here;

    if (strlen(keys) >= AIS_LINE_MAX)    /* an over-long list fails, is not truncated */
        return -1;
    active = ktomb_active(a);            /* gate the re-attach cleanup (cheap) */
    if (active < 0)
        return -1;
    katt_here = (katt_active(a) == 1);   /* likewise the detach-side cleanup */

    /* PASS 1 -- the AUTHORITATIVE keys field first, before the disposable index:
     * a posting inserted first would survive a failed or over-long rewrite as a
     * phantom key that the next compaction silently drops. Skipped when the caller
     * has just written the line with these keys (a brand-new record), the union
     * then being unable to change anything. */
    if (line_carries_keys)
        goto postings;                   /* the line was just written from these keys */
    p = keys;
    while ((got = keys_next(&p, tok, sizeof tok)) != 0) {
        char folded[AIS_KEY_MAX];        /* one key, not a whole line */
        int w, u;
        if (got < 0)
            return -1;
        if (tok[0] == '-' && tok[1] != '\0')
            continue;                    /* a detach never enters the keys field */
        w = attach_wins(a, id, tok, attach_ts, active);
        if (w < 0)
            return -1;
        if (!w)
            continue;
        if (!have_line_keys) {
            /* L.keys IS the union buffer: the one line-sized buffer this frame
             * carries, within the 512 KB thread stack the FFI seam runs on. */
            L.id = id;
            if (seek_record(a, &L) != 0)
                return -1;
            if (!L.found)                /* no line yet: nothing to mirror into */
                return -1;
            have_line_keys = 1;
        }
        if (key_fold_stored(tok, folded, sizeof folded) != 0)
            return -1;
        u = keys_union(L.keys, sizeof L.keys, folded);
        if (u < 0)
            return -1;                   /* would not fit: nothing written yet */
        if (u == 1)
            changed = 1;
    }
    if (changed && store_set_keys(a, id, L.keys) != 0)
        return -1;

    /* PASS 2 -- postings and detaches, walked from the start again. */
postings:
    p = keys;
    while ((got = keys_next(&p, tok, sizeof tok)) != 0) {
        if (got < 0)
            return -1;
        if (tok[0] == '-' && tok[1] != '\0') {       /* "-key": detach */
            const char *k = tok + 1;
            int had = ktomb_contains(a, id, k);      /* avoid duplicate entries */
            if (had < 0)
                return -1;
            if (post_remove(a, k, id) != 0)
                return -1;
            if (!had) {
                /* stamp the detach with the record's (ts, value-hash) so it can
                 * propagate as a K| key-tombstone in the merge stream (I1). */
                char kts[AIS_TS_MAX], khash[17];
                del_stamp(a, id, kts, sizeof kts, khash);
                if (ktomb_append(a, id, kts, khash, k) != 0)
                    return -1;
            }
            if (katt_here && katt_forget(a, id, k) != 0)
                return -1;                           /* it is no longer attached */
        } else {                                     /* attach */
            int w = attach_wins(a, id, tok, attach_ts, active);
            if (w < 0)
                return -1;
            if (!w)
                continue;                            /* the detach is newer: keep it */
            if (post_insert(a, tok, id) != 0)
                return -1;
            if (active && ktomb_remove(a, id, tok) != 0)
                return -1;                           /* clear any prior (older) detach */
            if (attach_ts == NULL && !line_carries_keys &&
                katt_note(a, id, tok) != 0)
                return -1;      /* a local attach to a record that already existed */
        }
    }
    return 0;
}

/* Copy KEYS into OUT keeping only attach tokens; a "-key" (detach) is dropped,
 * since a brand-new record has no key to detach. Returns 0/-1 (too long). */
static int keys_attach_only(const char *keys, char *out, size_t outsz)
{
    const char *p = keys;
    size_t used = 0;
    int n;

    /* Walked in place, not copied: this is inlined into ais_put_at_k, whose frame
     * already carries `clean[]`, and a second AIS_LINE_MAX buffer here is a third
     * of the way to the 512 KB thread stack the FFI seam runs on. The explicit
     * length guard below is what a copy would have given. */
    if (strlen(keys) >= AIS_LINE_MAX)
        return -1;
    out[0] = '\0';
    while (*p != '\0') {
        size_t len;
        while (*p == ' ' || *p == '\t')
            p++;
        len = strcspn(p, " \t");
        if (len == 0)
            break;                       /* trailing separators only */
        if (p[0] == '-' && len > 1) {
            p += len;
            continue;                    /* a detach token: never stored */
        }
        n = snprintf(out + used, outsz - used, "%s%.*s",
                     used ? " " : "", (int)len, p);
        if (n < 0 || used + (size_t)n >= outsz)
            return -1;
        used += (size_t)n;
        p += len;
    }
    /* Keys share the store line's '|' delimiter and are matched by their
     * key_encode() path form, which folds '|' and control bytes to '_'. Fold the
     * STORED keys the same way, or "a|b" shifts the value into the wrong field on
     * readback. Spaces (the token separators) are left intact. */
    {
        char *p;
        for (p = out; *p != '\0'; p++)
            if (*p == '|' || (unsigned char)*p < 0x20)
                *p = '_';
    }
    return 0;
}

/* Stamp a LOCAL edit. A failure does not fail the edit -- the data is already
 * saved -- but is not silent either: the record is then unprotected against a
 * peer's earlier delete. */
static void mts_stamp(const ais *a, long id)
{
    char now[AIS_TS_MAX];

    store_now(now, sizeof now);
    if (now[0] != '\0' && mts_set(a, id, now) != 0)
        fprintf(stderr, "ais: warning: could not record the edit time of record %ld\n"
                        "     (the index may be full or read-only); if another device\n"
                        "     deleted this record, the next sync may undo this edit\n", id);
}

/* ATTACH_TS is the same instant as TS except on a raised export (see ais.h): the
 * record decision runs on TS, the key-attach decision on ATTACH_TS. */
long ais_put_at(ais *a, const char *keys, const char *value, const char *ts)
{
    return ais_put_at_k(a, keys, value, ts, ts);
}

long ais_put_at_k(ais *a, const char *keys, const char *value, const char *ts,
                  const char *attach_ts)
{
    long id = 0, rc;
    int found;
    char clean[AIS_LINE_MAX];

    /* A save only ATTACHES keys; detaching a key is ais_update's job. Drop "-key". */
    if (keys_attach_only(keys, clean, sizeof clean) != 0)
        return -1;

    if (store_wlock(a) != 0)                 /* one writer at a time */
        return -1;
    if (store_load_next_id(a) != 0) {        /* fresh id under the lock */
        store_wunlock(a);
        return -1;
    }

    found = store_find_value(a, value, &id);
    if (found < 0) { rc = -1; goto out; }

    if (found) {
        if (tomb_contains(a, id) == 1) {     /* exists but deleted: last-write-wins */
            int win = 1;                     /* NULL ts = local now, always newest */
            if (ts != NULL) {
                char del_ts[AIS_TS_MAX];
                tomb_lookup(a, id, del_ts, sizeof del_ts);
                /* STRICTLY newer to resurrect, so a tie keeps the delete: deletes are
                 * sticky. Folder-sync I2 caveat: LWW here is wall-clock UTC, so a peer
                 * with a fast clock could stamp an add ahead of a genuinely-later delete
                 * and resurrect it. Bounded by inter-device skew; a hybrid logical clock
                 * is the post-v0 fix. */
                win = (strcmp(ts, del_ts) > 0);
            }
            if (!win) { rc = id; goto out; }            /* the delete is newer: stay deleted */
            if (tomb_remove(a, id) != 0) { rc = -1; goto out; }   /* resurrect */
            /* Say WHEN it came back, or the resurrection is LOCAL ONLY: a line
             * still carrying its creation time exports as an A| older than the
             * peer's tombstone, and the peer's returning D| kills it here again.
             * A LOCAL re-add restamps the line -- the user is saving this now. A
             * MERGE records it in `sts` and leaves the line alone, as
             * ais_merge_del does: a raised line changes what a K| detach later in
             * the SAME import pass compares against, making a tag removal depend
             * on the order the peer bundles were read in. */
            if (ts != NULL) {
                /* A failure must NOT abandon the put: the record is already
                 * un-tombstoned, so bailing leaves it resurrected with the
                 * arriving KEYS never applied. `sts` takes only the canonical
                 * 20-char form, and rejects a pre-v3 timestamp (19 chars, no 'Z')
                 * or a date-only one -- both legal in a store upgraded from format
                 * v2, both re-exported verbatim. The note is how the survival
                 * REACHES other devices: worth a warning, not a lost record. */
                if (ts[0] != '\0' && sts_set(a, id, ts) != 0)
                    fprintf(stderr, "ais: warning: record %ld came back, but the time it\n"
                                    "     came back at could not be recorded; another device\n"
                                    "     may delete it again on the next sync\n", id);
            } else {
                char now[AIS_TS_MAX];
                store_now(now, sizeof now);
                if (now[0] != '\0' && store_restamp(a, id, now) != 0) { rc = -1; goto out; }
                sts_clear(a, id);      /* the line now carries it; no raise needed */
            }
            /* Adopt the arriving key set instead of merging it into the one this
             * record had when it was deleted. That old field is a relic; keeping
             * it re-advertises keys other devices deliberately detached, at the
             * record's new (later) timestamp, which outranks their ktombs and
             * re-attaches the tag everywhere. Delete is delete, tags included.
             * The same either way it comes back: a local re-save describes the
             * record afresh exactly as an arriving one does. */
            if (resurrect_keys(a, id, clean) != 0) { rc = -1; goto out; }
        }
        rc = (ais_post_keys(a, clean, id, attach_ts, 0) != 0) ? -1 : id;  /* LWW vs a detach */
        /* Saving a value the index already holds is how a tag gets attached: an edit. */
        if (rc > 0 && ts == NULL)
            mts_stamp(a, id);
        goto out;
    }

    /* Not in the store -- but that is not the same as never having been here.
     * Compaction drops a deleted record's line while KEEPING its tombstone
     * (compact.c: "so an offline peer can't resurrect a deleted record"), so the
     * value stops resolving to an id and an id-keyed check stops seeing it. The
     * hash is what survives, and it is the same last-write-wins rule as above:
     * strictly newer resurrects, a tie keeps the delete. Without this a peer that
     * was switched off when the user deleted something pushes it back on the next
     * sync, and a deleted secret comes back after its ciphertext was shredded. */
    {
        char h[17], del_ts[AIS_TS_MAX];
        long dead_id = 0;
        int deleted;

        content_hash(value, h);
        deleted = tomb_lookup_hash(a, h, del_ts, sizeof del_ts, &dead_id);
        if (deleted < 0) { rc = -1; goto out; }
        if (deleted == 1) {
            int win = 1;                     /* NULL ts = the user saving it now */
            if (ts != NULL)
                win = (strcmp(ts, del_ts) > 0);
            if (!win) { rc = dead_id; goto out; }   /* the delete is newer: stay deleted */
            /* It wins, so the delete fact must go with it: retained, it would
             * export as a D| carrying this very hash and kill the new record on
             * every peer that applies it. */
            if (tomb_remove_hash(a, h) != 0) { rc = -1; goto out; }
        }
    }

    id = a->next_id;
    {
        char now[AIS_TS_MAX];
        const char *use_ts = ts;
        long off = -1;                      /* store_append reports where the line lands */
        int  ok  = off_consistent(a);
        if (use_ts == NULL) {
            store_now(now, sizeof now);     /* "" if the clock is unreadable */
            use_ts = now;
        }
        /* Reserve the id by persisting next_id BEFORE the record is written: a
         * crash or ENOSPC after this point only SKIPS id (a harmless gap), where
         * persisting after the durable append would let the next put reuse a live
         * id and collide two records. */
        a->next_id = id + 1;
        if (store_save_next_id(a) != 0) { a->next_id = id; rc = -1; goto out; }
        if (store_append(a, id, use_ts, clean, value, &off) != 0) { rc = -1; goto out; }
        if (ok && off_append(a, off) != 0)                        { rc = -1; goto out; }  /* keep "off" in lockstep */
    }
    /* 1: store_append above just wrote the line from `clean`, so the mirror
     * could not change anything -- skip its full-store scan on every new put. */
    if (ais_post_keys(a, clean, id, attach_ts, 1) != 0) { rc = -1; goto out; }
    debug("put: new id=%ld", id);
    rc = id;

out:
    store_wunlock(a);
    return rc;
}

long ais_put(ais *a, const char *keys, const char *value)
{
    return ais_put_at(a, keys, value, NULL);
}

/* Rewrite the keys field of every line belonging to one id (a multi-line record
 * repeats the same keys on each line). The keys field is authoritative: a key
 * posted only to idx/ is dropped when compaction rebuilds idx/ from the store, so
 * an attach that never lands here is silent data loss (LAYOUT.md, "the keys field
 * is authoritative"). */
struct keys_rewrite {
    FILE       *out;
    long        id;
    const char *keys;      /* NULL = keep the line's own keys */
    const char *ts;        /* NULL = keep the line's own ts    */
    int         matched;
};

static int keyrw_line(long id, const char *ts, const char *keys,
                      const char *value, void *vp)
{
    struct keys_rewrite *c = vp;
    const char *wkeys = keys;
    char wts[AIS_TS_MAX];

    snprintf(wts, sizeof wts, "%s", ts);     /* may be upgraded below */
    if (id == c->id) {
        const char *nk = (c->keys != NULL) ? c->keys : keys;
        int need;
        if (c->ts != NULL)                   /* restamp: the add-ts LWW compares */
            snprintf(wts, sizeof wts, "%s", c->ts);
        need = (wts[0] != '\0')
             ? snprintf(NULL, 0, "%ld|%s|%s|%s\n", id, wts, nk, value)
             : snprintf(NULL, 0, "%ld|%s|%s\n", id, nk, value);
        if (need < 0 || need >= AIS_LINE_MAX)   /* the edited line would not round-trip */
            return -1;
        wkeys = nk;
        c->matched = 1;
        /* A legacy (no-ts) line is re-emitted as "id|keys|value", so a keys field
         * that parses as a date lands in the ts slot: every field shifts right and
         * the VALUE becomes a key. Stamp a real ts instead, upgrading the line.
         * Only on a pre-v2 line whose new keys start with a date-shaped token. */
        if (wts[0] == '\0' && c->keys != NULL && store_looks_like_ts(c->keys)) {
            char now[AIS_TS_MAX];
            store_now(now, sizeof now);
            if (now[0] != '\0' &&
                snprintf(NULL, 0, "%ld|%s|%s|%s\n", id, now, nk, value) < AIS_LINE_MAX)
                snprintf(wts, sizeof wts, "%s", now);
            else
                return -1;               /* cannot write a readable line: refuse */
        }
    }
    if (wts[0] != '\0')
        fprintf(c->out, "%ld|%s|%s|%s\n", id, wts, wkeys, value);   /* v2/v3 */
    else
        fprintf(c->out, "%ld|%s|%s\n", id, wkeys, value);           /* legacy */
    return 0;
}

/* Write KEYS into id's store lines. Caller holds the write lock. 0, or -1 (the
 * store is left untouched on any failure -- nothing is renamed). */
static int store_rewrite_line(ais *a, long id, const char *keys, const char *ts)
{
    char storep[AIS_PATH_MAX], newp[AIS_PATH_MAX], offp[AIS_PATH_MAX];
    struct keys_rewrite c;
    FILE *out;

    if (snprintf(storep, sizeof storep, "%s/store", a->dir) >= (int)sizeof storep ||
        snprintf(newp, sizeof newp, "%s/store.new", a->dir) >= (int)sizeof newp)
        return -1;

    out = fopen(newp, "w");
    if (out == NULL)
        return -1;
    c.out = out; c.id = id; c.keys = keys; c.ts = ts; c.matched = 0;

    if (store_each_record(a, keyrw_line, &c) != 0 || !c.matched) {
        fclose(out); remove(newp);
        return -1;
    }
    /* Not `fflush(out) != 0 || fclose(out) != 0`: short-circuiting on a failed
     * fflush (ENOSPC, EIO) would return with the stream still open, and remove()
     * on an open file fails outright on Windows, leaving store.new behind. */
    if (fflush(out) != 0) { fclose(out); remove(newp); return -1; }
    if (fclose(out) != 0) { remove(newp); return -1; }
    if (rename(newp, storep) != 0) { remove(newp); return -1; }

    /* "off" now holds stale byte offsets; it is a pure accelerator that
     * ais_record falls back past, so drop it and let it rebuild. */
    if (snprintf(offp, sizeof offp, "%s/off", a->dir) < (int)sizeof offp)
        remove(offp);
    return 0;
}

/* Replace the record's keys field, keeping its ts. */
static int store_set_keys(ais *a, long id, const char *keys)
{
    return store_rewrite_line(a, id, keys, NULL);
}

/* Replace the record's ts, keeping its keys. The ts IS the add-ts that merging
 * compares against a peer's tombstone (MERGE.md, "last-write-wins by ts"), so a
 * record that comes back to life has to carry the time it came back or it loses
 * to its own old delete for ever, on every device. */
static int store_restamp(ais *a, long id, const char *ts)
{
    return store_rewrite_line(a, id, NULL, ts);
}

/* Copy TOK into OUT folding '|' and control bytes to '_', as keys_attach_only does
 * for a put: a raw "a|b" would shift the value into the wrong field on readback and
 * disagree with the key_encode() form the index matches on. ais_update passes the
 * user's string through unsanitized, so the fold has to happen here or an attach
 * corrupts the line. Returns 0/-1 (too long). */
static int key_fold_stored(const char *tok, char *out, size_t sz)
{
    size_t i;

    for (i = 0; tok[i] != '\0'; i++) {
        if (i + 1 >= sz)
            return -1;
        out[i] = (tok[i] == '|' || (unsigned char)tok[i] < 0x20) ? '_' : tok[i];
    }
    if (i >= sz)
        return -1;
    out[i] = '\0';
    return 0;
}

/* Append TOK to KEYS unless an encode-equivalent key is already there. Identity is
 * the key_encode() form, matching the postings this mirrors: idx/ holds one entry
 * for "Doc", "doc" and "DOC", so treating them as three tokens grows the keys field
 * without bound on ordinary re-puts (a UI that titlecases a tag is enough) until the
 * line no longer fits and the record becomes unstorable. The first spelling seen is
 * kept. Returns 1 if added, 0 if already present, -1 if the result would not fit. */
/* Is TOK (already stored-form) one of the space-separated tokens in KEYS? */
static int keys_contains(const char *keys, const char *tok)
{
    const char *p = keys;

    while (*p != '\0') {
        size_t n;
        while (*p == ' ' || *p == '\t') p++;
        for (n = 0; p[n] != '\0' && p[n] != ' ' && p[n] != '\t'; n++)
            ;
        if (n == 0)
            break;
        if (n == strlen(tok) && strncmp(p, tok, n) == 0)
            return 1;
        p += n;
    }
    return 0;
}

static int keys_union(char *keys, size_t sz, const char *tok)
{
    const char *p = keys;
    char enc_tok[AIS_KEY_MAX], enc_have[AIS_KEY_MAX];
    size_t tlen = strlen(tok), klen;

    if (key_encode(tok, enc_tok, sizeof enc_tok) != 0)
        return -1;

    while (*p != '\0') {                       /* scan the existing tokens */
        size_t n;
        char one[AIS_KEY_MAX];
        while (*p == ' ' || *p == '\t') p++;
        for (n = 0; p[n] != '\0' && p[n] != ' ' && p[n] != '\t'; n++)
            ;
        if (n == 0)
            break;                             /* trailing whitespace only */
        if (n < sizeof one) {
            memcpy(one, p, n);
            one[n] = '\0';
            if (key_encode(one, enc_have, sizeof enc_have) == 0 &&
                strcmp(enc_have, enc_tok) == 0)
                return 0;                      /* already present, some spelling */
        }
        p += n;
    }
    klen = strlen(keys);
    if (klen + 1 + tlen + 1 > sz)
        return -1;
    if (klen > 0)
        keys[klen++] = ' ';
    memcpy(keys + klen, tok, tlen + 1);
    return 1;
}

/* Replace record ID's key set with WANT, dropping the postings of the keys that
 * are no longer in it. No ktomb is written: this device is not claiming those keys
 * were removed, it is adopting the description of a record it no longer has. */
AIS_NOINLINE static int resurrect_keys(ais *a, long id, const char *want)
{
    struct add_lookup L;
    char *tok, *save;

    L.id = id;
    if (seek_record(a, &L) != 0)
        return -1;
    if (!L.found)
        return 0;

    /* Tokenise L.keys IN PLACE. Static and called once, so it inlines into the
     * primary save path and its frame is reserved on EVERY put, including those
     * that never reach here; a second AIS_LINE_MAX copy of the keys plus a
     * line-wide enc[] puts that frame over the 512 KB thread stack the FFI seam
     * runs on. L.keys is not read after this loop: store_set_keys writes WANT. */
    for (tok = strtok_r(L.keys, " ", &save); tok != NULL; tok = strtok_r(NULL, " ", &save)) {
        char enc[AIS_KEY_MAX];           /* one key, not a whole line */
        if (key_fold_stored(tok, enc, sizeof enc) != 0)
            continue;
        if (keys_contains(want, enc))
            continue;
        if (post_remove(a, enc, id) != 0)
            return -1;
    }
    return store_set_keys(a, id, want);
}

static int add_seek(long id, const char *ts, const char *keys,
                    const char *value, void *vp)
{
    struct add_lookup *L = vp;
    (void)ts;
    (void)value;
    if (id == L->id) {
        L->found = 1;
        snprintf(L->keys, sizeof(L->keys), "%s", keys);
        return -1;   /* stop: first occurrence carries the keys */
    }
    return 0;
}

/* add_seek's work AND the duplicate-value check, in ONE store pass; separately the
 * guard costs a second full scan on every --add. The comparison is by 16-char digest
 * first and strcmp only on a digest hit, so a --doc blob or a long note costs 16
 * bytes per line instead of its whole length. The strcmp is required: FNV-1a is
 * documented as "NOT a security hash" and an unconfirmed collision would refuse a
 * legitimate distinct value. The wire cannot confirm -- a delete arrives as
 * D|ts|hash, with no value. */
static int add_seek_dup(long id, const char *ts, const char *keys,
                        const char *value, void *vp)
{
    struct add_lookup *L = vp;
    char h[17];
    (void)ts;

    if (id == L->id && !L->found) {
        L->found = 1;
        snprintf(L->keys, sizeof(L->keys), "%s", keys);
    }
    if (id != L->id) {
        content_hash(value, h);
        if (strcmp(h, L->whash) == 0 && strcmp(value, L->want) == 0) {
            L->dup_id = id;
            return -1;              /* decisive: stop early */
        }
    }
    return 0;
}

/* The body of ais_add. LOCAL separates a user's own --add -- an edit of THIS
 * device's copy, which must outrank an earlier peer delete -- from an arriving M|
 * link, a fact the sending device already timed. Stamping the local edit clock on
 * an import makes a record's fate depend on the readdir order of the peer bundles,
 * the non-determinism C|/sts exist to remove. LAYOUT.md: "an import does not: an
 * arriving record carries its own time". */
static int add_link(ais *a, long id, const char *value, int local)
{
    struct add_lookup L;
    int scan, rc = -1;

    if (store_wlock(a) != 0)
        return -1;

    if (tomb_contains(a, id) != 0)      /* deleted: --add would revive it silently */
        goto out;

    /* One pass finds this record's keys AND refuses a value another record already
     * holds: store_find_value resolves a duplicated value to the FIRST match, so
     * anything addressing a record by its value acts on the wrong one, a peer
     * collapses the two on merge, and a later delete of either takes both. */
    L.id = id;
    L.found = 0;
    L.keys[0] = '\0';
    L.want = value;
    L.dup_id = 0;
    content_hash(value, L.whash);

    scan = store_each_record(a, add_seek_dup, &L);
    if (scan < -1)          /* a real error (callback stops with -1) */
        goto out;
    if (L.dup_id != 0) {
        rc = -2;            /* another record owns this value */
        goto out;
    }
    if (!L.found)
        goto out;

    /* Continuation line: same id, same keys field (already posted to this id, so no
     * re-post), the new value, stamped with its own later save time. */
    /* Mark id multi-line BEFORE appending the continuation. A "multi" entry only
     * forces ais_record onto the full-scan path, which returns every line, so a
     * mark with no matching second line is harmless; a second line the multi set
     * does not know about makes the fast path hide it. */
    if (multi_append(a, id) != 0)           /* id now has >1 line */
        goto out;
    {
        char ts[AIS_TS_MAX];
        store_now(ts, sizeof(ts));
        if (store_append(a, id, ts, L.keys, value, NULL) != 0)
            goto out;
    }
    debug("add: appended link to id=%ld", id);
    if (local)
        mts_stamp(a, id);          /* another link on an existing record is an edit */
    rc = 0;
out:
    store_wunlock(a);
    return rc;
}

int ais_add(ais *a, long id, const char *value)
{
    return add_link(a, id, value, 1);
}

/* Attaches and detaches through ais_post_keys (see ais.h). */
int ais_update(ais *a, long id, const char *keys)
{
    struct add_lookup L;
    int rc = -1, t;

    if (store_wlock(a) != 0)
        return -1;

    t = tomb_contains(a, id);
    if (t != 0)                         /* 1 = deleted, -1 = error: refuse */
        goto out;

    L.id = id;
    L.found = 0;
    L.keys[0] = '\0';
    if (store_each_record(a, add_seek, &L) < -1)   /* real error (cb stops at -1) */
        goto out;
    if (!L.found)                       /* no such id */
        goto out;

    rc = (ais_post_keys(a, keys, id, NULL, 0) != 0) ? -1 : 0;  /* local edit: now, always wins */
    if (rc == 0)
        mts_stamp(a, id);
out:
    store_wunlock(a);
    if (rc == 0)
        debug("update: id=%ld keys='%s'", id, keys);
    return rc;
}

/* Streams the store to a temp file, rewriting the ONE line whose id == ID and whose
 * value exactly equals OLD_VALUE (its ts and keys carried through verbatim); every
 * other line is copied byte-for-byte, legacy "id|keys|value" lines kept legacy. The
 * value is not in the key index, so the postings, next_id and multi are left
 * alone; tomb, ktomb and katt are touched only to let the edit travel (below). */
struct setval_ctx {
    FILE       *out;
    long        id;
    const char *old_value, *new_value;
    int         matched, error;
    int         seen;      /* a line of ID has gone by */
    int         first;     /* the replaced line was the record's FIRST: its hash changes */
};

static int setval_line(long id, const char *ts, const char *keys,
                       const char *value, void *vp)
{
    struct setval_ctx *c = vp;
    const char *wval = value;

    if (!c->matched && id == c->id && strcmp(value, c->old_value) == 0) {
        int need = (ts[0] != '\0')
                 ? snprintf(NULL, 0, "%ld|%s|%s|%s\n", id, ts, keys, c->new_value)
                 : snprintf(NULL, 0, "%ld|%s|%s\n", id, keys, c->new_value);
        c->first = !c->seen;
        if (need < 0 || need >= AIS_LINE_MAX) {   /* the edited line would not round-trip */
            c->error = 1;
            return -1;
        }
        wval = c->new_value;
        c->matched = 1;
    }
    if (id == c->id)
        c->seen = 1;
    if (ts[0] != '\0')
        fprintf(c->out, "%ld|%s|%s|%s\n", id, ts, keys, wval);   /* v2 */
    else
        fprintf(c->out, "%ld|%s|%s\n", id, keys, wval);          /* legacy */
    return 0;
}

/* A value fed verbatim (an "aisc:"/"@blob" marker) is compared and replaced as a
 * plain string: no re-encryption, and no blob file is touched. */
int ais_set_value(ais *a, long id, const char *old_value, const char *new_value)
{
    char storep[AIS_PATH_MAX], newp[AIS_PATH_MAX], offp[AIS_PATH_MAX];
    struct setval_ctx c;
    FILE *out;
    int rc = -1;

    if (old_value == NULL || new_value == NULL)
        return -1;
    /* A record is ONE line: an embedded newline ends the fgets on read and drops
     * everything after it. store_append refuses this on the put path; the in-place
     * edit has to too, or the tail becomes an orphan (unrecoverable data loss). */
    if (strpbrk(new_value, "\r\n") != NULL) {
        fprintf(stderr, "ais: value spans multiple lines -- use --doc for multi-line/large values\n");
        return -1;
    }
    if (snprintf(storep, sizeof storep, "%s/store", a->dir) >= (int)sizeof storep ||
        snprintf(newp, sizeof newp, "%s/store.new", a->dir) >= (int)sizeof newp)
        return -1;

    if (store_wlock(a) != 0)
        return -1;

    /* Refuse a deleted id, as ais_update does. Editing a tombstoned record would
     * also leave the tomb's content hash pointing at a value that no longer
     * exists, so the delete could never propagate to a peer. */
    if (tomb_contains(a, id) != 0) {
        store_wunlock(a);
        return -1;
    }
    /* Refuse a value another record already holds: a value is identity (ais.h), so
     * a peer collapses two records sharing one and a delete of either takes both. */
    {
        long other = 0;
        int  dup = store_find_value(a, new_value, &other);
        if (dup < 0) { store_wunlock(a); return -1; }
        if (dup && other != id) { store_wunlock(a); return -1; }
    }

    out = fopen(newp, "w");
    if (out == NULL) {
        store_wunlock(a);
        return -1;
    }
    c.out = out;
    c.id = id;
    c.old_value = old_value;
    c.new_value = new_value;
    c.matched = 0;
    c.error = 0;
    c.seen = 0;
    c.first = 0;

    if (store_each_record(a, setval_line, &c) != 0 || !c.matched) {
        fclose(out);                       /* io error, or no matching line: no rename */
        remove(newp);
        goto out;
    }
    if (fflush(out) != 0) {                /* rename below is atomic; no fsync, matching compact.c */
        fclose(out);
        remove(newp);
        goto out;
    }
    if (fclose(out) != 0) { remove(newp); goto out; }
    if (rename(newp, storep) != 0) { remove(newp); goto out; }

    /* "off" now points at stale byte offsets. It is a pure, rebuildable accelerator
     * ais_record falls back past, so drop it rather than risk a wrong offset. */
    if (snprintf(offp, sizeof offp, "%s/off", a->dir) < (int)sizeof offp)
        remove(offp);
    mts_stamp(a, id);
    /* The edit has to reach the peers, and the stream already has the verbs: the
     * old value is retired as a D|ts|hash, the fact every delete travels as,
     * under id 0, which names no record, so the edited line stays live here
     * while every peer drops its copy. The new value then arrives there as an
     * ordinary record. What a peer cannot tell from that is that the two were
     * one record: a delete it makes of the OLD value after this edit names a
     * hash this index no longer holds and is skipped, so the edit survives it,
     * as a fresh save of the new text would. If this index once deleted the
     * NEW value, that tombstone would export as a D| carrying the edit's own
     * hash and kill it on every peer: it goes, and the record exports raised to
     * now (sts), which is what lets it outrank the copy of that tombstone a
     * peer still holds. ktomb/katt name the record by its first value's hash;
     * when that is the value replaced they are re-keyed, or a detach made before
     * the edit would travel under a name no peer has. */
    if (strcmp(old_value, new_value) != 0) {
        char h[17], now[AIS_TS_MAX];
        store_now(now, sizeof now);
        content_hash(old_value, h);
        if (now[0] != '\0' && tomb_append(a, 0, now, h) != 0)
            fprintf(stderr, "ais: warning: record %ld was edited, but the old value could\n"
                            "     not be retired; a device that syncs may send it back\n", id);
        content_hash(new_value, h);
        if (tomb_lookup_hash(a, h, NULL, 0, NULL) == 1 && tomb_remove_hash(a, h) == 0 &&
            now[0] != '\0')
            sts_set(a, id, now);
        if (c.first) {
            ktomb_rehash(a, id, h);
            katt_rehash(a, id, h);
        }
    }
    /* The replaced value may have been the only reference to a file THIS INDEX
     * made. Retiring it is a delete of a payload, so it goes through the SAME
     * seam a merged delete uses (ais_on_discard) rather than teaching the engine
     * about blobs: the CLI disposed of it by hand and the FFI and the web server
     * did not, so replacing a document's value from the app or the browser
     * orphaned its file -- and feed.c streams the whole of blobs/, so that
     * orphan then rode every export to every peer, forever. AFTER the rename: a
     * refused edit must never destroy a payload the store still points at. */
    if (a->discard != NULL)
        a->discard(old_value, a->discard_ctx);
    rc = 0;

out:
    store_wunlock(a);
    if (rc == 0)
        debug("set_value: id=%ld value replaced", id);
    return rc;
}

/* Grab a record's value by id (via ais_record) so del can content-hash it for a
 * portable, compaction-proof tombstone. */
struct del_value {
    long id;
    char hash[17];                    /* the value's content hash, taken in the callback */
    char ts[AIS_TS_MAX];              /* the record's CREATION ts: the hash salt */
    int  found;
};

/* Grab the record's creation ts as well as its value. The ts is field 2 of the
 * store line and ais_record only yields values, so this reads the line itself --
 * through the same off/store_record_at seek ais_record uses, not a full scan,
 * because it runs once per record inside --del-under and --untag. */
static int del_seek_line(long id, const char *ts, const char *keys,
                         const char *value, void *ctx)
{
    struct del_value *D = ctx;
    (void)keys;
    if (id != D->id)
        return 0;
    /* Hashed HERE rather than copied out: an AIS_LINE_MAX of value in this struct
     * puts del_stamp's frame -- reached by every save to an existing value, through
     * katt_note -- 64 KB over the 512 KB thread stack the FFI seam runs on. The
     * hash is all either caller wants. */
    content_hash(value, D->hash);
    snprintf(D->ts, sizeof D->ts, "%s", ts);
    D->found = 1;
    return 1;                          /* first line wins */
}

/* Stamp a tombstone for id: ts = now, hash = content hash of the record's value (so
 * the deletion is portable for cross-device merge), or "" if the id is already gone. */
static void del_stamp(ais *a, long id, char *ts, size_t tsz, char hash[17])
{
    struct del_value D;
    long offset;

    D.id = id;
    D.found = 0;
    D.hash[0] = '\0';
    D.ts[0] = '\0';
    if (off_get(a, id, &offset) != 1 ||
        store_record_at(a, id, offset, del_seek_line, &D) < 0 || !D.found)
        store_each_record(a, del_seek_line, &D);   /* no accelerator: scan */
    store_now(ts, tsz);
    if (D.found)
        memcpy(hash, D.hash, sizeof D.hash);
    else
        hash[0] = '\0';
}

/* Every value of a record a delete just retired -- local or arriving from a peer
 * -- offered to the front end's disposer. */
static int mdel_discard_value(long id, const char *value, void *vp)
{
    ais *a = vp;
    (void)id;
    a->discard(value, a->discard_ctx);
    return 0;
}

int ais_del(ais *a, long id)
{
    char ts[AIS_TS_MAX], hash[17];
    int rc, dead;

    if (store_wlock(a) != 0)
        return -1;
    /* Deleting an already-deleted record says nothing new, and a tombstone is kept
     * for the life of the index -- a second one makes every peer re-scan its whole
     * store for the same fact on every future import. */
    dead = tomb_contains(a, id);
    if (dead < 0) {
        store_wunlock(a);
        return -1;
    }
    if (dead == 1) {
        store_wunlock(a);
        debug("del: id=%ld already tombstoned", id);
        return 0;
    }
    del_stamp(a, id, ts, sizeof ts, hash);
    rc = tomb_append(a, id, ts, hash);
    if (rc == 0) {
        mts_clear(a, id);          /* delete is delete: keep no note of when it was touched */
        sts_clear(a, id);
        katt_forget(a, id, NULL);  /* nor of when its tags went on */
        /* And the payload goes with the record, through the same seam a peer's
         * delete uses (ais_merge_del_many). Each front end used to do this by
         * hand before calling in, which is three copies of one rule and one
         * ordering bug: shredding first meant a REFUSED delete had already
         * destroyed the payload of a record that stayed. The store line stands
         * until compaction, so the values still read back here. */
        if (a->discard != NULL)
            ais_record(a, id, mdel_discard_value, a);
    }
    store_wunlock(a);
    if (rc == 0)
        debug("del: tombstoned id=%ld", id);
    return rc;
}

int ais_del_key(ais *a, const char *key)
{
    post_stream s;
    int n = 0, rc;

    if (store_wlock(a) != 0)
        return -1;
    if (post_open(a, key, &s) != 0) {
        store_wunlock(a);
        return -1;
    }

    for (; s.alive; post_next(&s)) {
        char ts[AIS_TS_MAX], hash[17];
        /* A posting still lists ids whose records are already tombstoned (removal
         * is physical only at compaction). Those are RE-STAMPED, so a peer's add
         * dated between the original delete and this one stays suppressed, but not
         * COUNTED: the count is live records, matching the caller's preview. */
        int dead = tomb_contains(a, s.head);
        if (dead < 0) {
            rc = -1;
            goto cleanup;
        }
        del_stamp(a, s.head, ts, sizeof ts, hash);
        /* Re-stamping an already-dead record REPLACES its tombstone rather than
         * adding one: "this key is deleted as of now" is one fact, not a running
         * tally. Appending grows the tomb without bound on a repeated --del-under,
         * and every duplicate costs each peer a full store scan on every import. */
        if (dead == 1 && tomb_remove(a, s.head) != 0) {
            rc = -1;
            goto cleanup;
        }
        if (tomb_append(a, s.head, ts, hash) != 0) {
            rc = -1;
            goto cleanup;
        }
        /* Delete is delete, by whichever door: the edit clock and key-attach notes
         * of a tombstoned record left behind export as T| lines asserting a key on
         * a record the SAME stream tombstones -- junk every peer pays a store scan
         * for and its own katt then re-propagates. ais_del and mdel_apply clear all
         * three too. */
        mts_clear(a, s.head);
        sts_clear(a, s.head);
        katt_forget(a, s.head, NULL);
        if (!dead)
            n++;
    }
    rc = n;

cleanup:
    post_close(&s);
    store_wunlock(a);
    debug("del_key: '%s' tombstoned %d records", key, n);
    return rc;
}

int ais_untag_key(ais *a, const char *key)
{
    char enc[AIS_KEY_MAX], detach[AIS_KEY_MAX + 2];
    long ids[64], after = LONG_MIN;
    int n = 0, i, got;
    post_stream s;

    if (key == NULL || key[0] == '\0')
        return -1;
    /* Address the key the POSTING uses. ais_post_keys splits its argument on
     * whitespace, so a raw "-a b" detaches "a" and ATTACHES "b" while the posting
     * being polled is "a_b": the loop never shrinks it and spins forever. The
     * encoded form never contains whitespace, so both sides name the same thing. */
    if (key_encode(key, enc, sizeof enc) != 0)
        return -1;
    if (snprintf(detach, sizeof detach, "-%s", enc) >= (int)sizeof detach)
        return -1;

    /* Collect ids FIRST, then mutate: ais_update rewrites the store and the
     * posting, so streaming that posting while changing it reads a file being
     * renamed out from under the stream. AFTER is a strictly advancing cursor, so
     * the loop terminates even when an id cannot be consumed; it starts below every
     * id, including the non-positive ones a hand-edited or truncated index can
     * hold, which would otherwise leave the key alive for ever. */
    for (;;) {
        if (post_open(a, enc, &s) != 0)      /* reads are lock-free by design */
            return -1;
        for (got = 0; s.alive && got < (int)(sizeof ids / sizeof ids[0]); post_next(&s))
            if (s.head > after)
                ids[got++] = s.head;
        post_close(&s);

        if (got == 0)
            break;
        /* The MAX, not the last: a posting is written ascending, but a hand-edited
         * one need not be, and taking the last entry could move the cursor
         * BACKWARDS and re-collect ids the next pass had already consumed. */
        after = ids[0];
        for (i = 1; i < got; i++)
            if (ids[i] > after)
                after = ids[i];

        for (i = 0; i < got; i++) {
            char kts[AIS_TS_MAX], khash[17];
            int dead, had, j, dup = 0;

            /* A duplicated entry (again: a hand edit) must be counted ONCE. The
             * copies need not be adjacent, so compare against the whole batch;
             * across batches the cursor already excludes them. */
            for (j = 0; j < i; j++)
                if (ids[j] == ids[i]) {
                    dup = 1;
                    break;
                }
            if (dup)
                continue;

            dead = tomb_contains(a, ids[i]);
            if (dead < 0)
                return -1;

            if (dead == 0) {
                if (ais_update(a, ids[i], detach) == 0) {
                    n++;
                    continue;
                }
                /* ais_update also refuses an id with NO store line, which a
                 * posting can name after a hand edit or a store restored without
                 * its idx/; failing there wedges the key on every retry. */
                del_stamp(a, ids[i], kts, sizeof kts, khash);
                if (khash[0] != '\0')
                    return -1;        /* the record is real: a genuine failure */
            } else {
                /* Tombstoned, so ais_update refuses it, but the detach must still
                 * be RECORDED: pruning the posting alone leaves the key in the
                 * authoritative keys field with nothing to mask it, so a resurrect
                 * brings the tag back at the next compaction and no K| reaches a
                 * peer still holding the record live. */
                had = ktomb_contains(a, ids[i], enc);
                if (had < 0)
                    return -1;
                del_stamp(a, ids[i], kts, sizeof kts, khash);
                if (!had && ktomb_append(a, ids[i], kts, khash, enc) != 0)
                    return -1;
            }
            /* Not counted either way: nothing live lost a tag here. */
            if (store_wlock(a) != 0)  /* post_remove is a WRITER (LOCKING.md) */
                return -1;
            if (post_remove(a, enc, ids[i]) != 0) {
                store_wunlock(a);
                return -1;
            }
            store_wunlock(a);
        }
    }
    debug("untag_key: '%s' detached from %d records", enc, n);
    return n;
}

/* Find a local record whose value hashes to the target (record identity = value).
 * The seek reports the store's own ts and nothing else: a K| detach and an M| link
 * resolve against it, an unrelated edit saying nothing about one key -- an edit time
 * answering a K| would let any tag added on one device silently re-attach a tag
 * another removed. Only the record-delete decision consults the edit clock
 * (mdel_apply). */
struct mdel_ctx { const char *hash; long id; char ts[AIS_TS_MAX]; int found; };
static int mdel_seek(long id, const char *ts, const char *keys, const char *value, void *vp)
{
    struct mdel_ctx *M = vp;
    char h[17];
    (void)keys;
    /* Identity is the value and NOTHING else -- no creation-ts salt. Two devices
     * that independently save the same value stamp it at different times, so a
     * salted digest differs and a delete stops crossing between them. Identity has
     * to come from what both sides agree on with nothing shared. */
    content_hash(value, h);
    if (strcmp(h, M->hash) == 0) {
        M->id = id;
        snprintf(M->ts, sizeof M->ts, "%s", ts);
        M->found = 1;
        return -1;   /* stop */
    }
    return 0;
}

/* The import side of the M| verb. Each value is its own store line, so without M|
 * a plain export emits each as its own A| and every restore SPLITS one record
 * into several. */
int ais_merge_addval(ais *a, const char *hash, const char *value)
{
    struct mdel_ctx M;

    if (hash == NULL || hash[0] == '\0' || value == NULL || value[0] == '\0')
        return -1;
    M.hash = hash;
    M.id = 0;
    M.found = 0;
    M.ts[0] = '\0';
    store_each_record(a, mdel_seek, &M);
    if (!M.found)
        return 0;                       /* no such record here: nothing to do */
    if (tomb_contains(a, M.id) == 1)
        return 0;                       /* deleted here: do not resurrect via a link */
    {
        /* ais_add appends unconditionally, and folder sync re-imports the same
         * bundle every pass, so this guard is what makes a replay idempotent. */
        long already = 0;
        if (store_find_value(a, value, &already) == 1)
            return 0;                   /* this index already holds that value */
    }
    return (add_link(a, M.id, value, 0) == 0) ? 0 : -1;   /* an import does not stamp */
}

/* One delete fact against the local record it named: the last-write-wins decision,
 * identical whether the fact arrived alone or inside a batch. LINE_TS is the store
 * line's own timestamp. 0/-1. */
static int mdel_apply(ais *a, long id, const char *line_ts,
                      const char *hash, const char *ts)
{
    char add_ts[AIS_TS_MAX];

    /* last-write-wins: delete iff the incoming delete is at least as new as the local
     * add and the value is not already deleted; an absent value has nothing to delete.
     * Returns 1 when this call tombstoned the record, 0 when it left it alone, -1 on
     * error: only a 1 may dispose of the record's payload. */
    if (tomb_contains(a, id) != 0)
        return 0;
    /* The EFFECTIVE time: a record edited after a peer deleted it must not lose to
     * that delete just because its creation time is older. */
    mts_effective(a, id, line_ts, add_ts, sizeof add_ts);
    if (strcmp(ts, add_ts) >= 0) {
        int rc = tomb_append(a, id, ts, hash);
        if (rc == 0) {
            mts_clear(a, id);            /* delete is delete, however it arrived */
            sts_clear(a, id);
            katt_forget(a, id, NULL);
            return 1;                    /* tombstoned HERE, now: the caller disposes */
        }
        return rc;
    }
    /* The local EDIT is newer, so the record stays. Record the time it now exports
     * at, or the decision is LOCAL ONLY: the line goes on exporting its creation
     * time, the peer that deleted it keeps its tombstone, and its D| comes back
     * every round -- the record flapping on every device but this one. The
     * resurrect path's rule, reached from the other side.
     *
     * Only when the edit time is LATER than the line: restamping for a stale delete
     * that already loses on creation time rewrites the entire store (and drops the
     * "off" accelerator) on every sync round, for no effect.
     *
     * Only as far as it has to go: one second past the delete, not all the way to
     * the edit time. The exported timestamp also decides key attaches, so every
     * second of raise sweeps up unrelated key tombstones on other devices and
     * re-attaches tags they removed.
     *
     * And kept OUT of the store line: written there, a K| detach arriving later in
     * the same import pass compares against the raised line and loses, while one
     * arriving earlier compares against the creation time and wins -- the
     * difference being the order readdir returned the peer bundles in. */
    char want[AIS_TS_MAX], have[AIS_TS_MAX];
    if (store_ts_next_second(ts, want, sizeof want) != 0 ||
        strcmp(want, add_ts) > 0)
        snprintf(want, sizeof want, "%s", add_ts);
    if (strcmp(want, line_ts) <= 0)
        return 0;                            /* the line already outranks it */
    if (sts_get(a, id, have, sizeof have) == 1 && strcmp(want, have) <= 0)
        return 0;                            /* already recorded, at least this high */
    a->survivals++;      /* news the peer cannot have yet: see ais.h */
    if (sts_set(a, id, want) != 0)
        fprintf(stderr, "ais: warning: record %ld survived a delete from another\n"
                        "     device, but that could not be recorded; that device\n"
                        "     may delete it again on the next sync\n", id);
    return 0;
}

/* Resolve a whole batch of delete facts in ONE store pass: each store line is
 * hashed once and offered to every fact still unresolved. Applying stays in STREAM
 * order: nothing an apply writes (tomb, mts, sts) is read by a seek, but two facts
 * can name two values of ONE record, and the order then decides which value's hash
 * the tombstone carries onward to the peers. */
struct mbatch_ctx { const ais_del_fact *f; int n, left;
                    long id[AIS_MERGE_BATCH]; char ts[AIS_MERGE_BATCH][AIS_TS_MAX]; };
static int mbatch_seek(long id, const char *ts, const char *keys, const char *value, void *vp)
{
    struct mbatch_ctx *B = vp;
    char h[17];
    int i;

    (void)keys;
    content_hash(value, h);
    for (i = 0; i < B->n; i++) {
        if (B->id[i] != 0 || strcmp(h, B->f[i].hash) != 0)
            continue;
        B->id[i] = id;          /* the FIRST matching line, where a single seek stops */
        snprintf(B->ts[i], sizeof B->ts[i], "%s", ts);
        B->left--;
    }
    return (B->left == 0) ? -1 : 0;      /* nothing left to look for: stop reading */
}

int ais_merge_del_many(ais *a, const ais_del_fact *facts, int n)
{
    struct mbatch_ctx B;
    int i, rc = 0;

    if (a == NULL || facts == NULL || n < 0 || n > AIS_MERGE_BATCH)
        return -1;
    if (n == 0)
        return 0;
    B.f = facts;
    B.n = n;
    B.left = n;
    for (i = 0; i < n; i++) {
        B.id[i] = 0;                     /* 0 = no local record named by this fact */
        B.ts[i][0] = '\0';
    }
    if (store_wlock(a) != 0)
        return -1;
    store_each_record(a, mbatch_seek, &B);
    for (i = 0; i < n; i++) {
        int applied;
        if (B.id[i] == 0)
            continue;
        applied = mdel_apply(a, B.id[i], B.ts[i], facts[i].hash, facts[i].ts);
        if (applied < 0)
            rc = -1;
        else if (applied == 1 && a->discard != NULL)
            /* The record is tombstoned but its store line stands until
             * compaction, so its values still read back: hand each to the front
             * end, which knows which of them name files this index made. Reads
             * take no lock, so doing it under the write lock is safe. */
            ais_record(a, B.id[i], mdel_discard_value, a);
    }
    store_wunlock(a);
    return rc;
}

int ais_merge_del(ais *a, const char *hash, const char *ts)
{
    ais_del_fact f;

    if (hash == NULL || ts == NULL)
        return -1;
    snprintf(f.hash, sizeof f.hash, "%s", hash);
    snprintf(f.ts, sizeof f.ts, "%s", ts);
    return ais_merge_del_many(a, &f, 1);
}

/* One attach fact against the local record it named: the decision, identical
 * whether the fact arrived alone or inside a batch. Records the attach time so it
 * can be compared against and re-propagated; a fact already applied is recognised
 * by that same note. Returns 0. */
static int att_apply(ais *a, long id, const char *hash, const char *key, const char *ts)
{
    char have[AIS_TS_MAX];
    int noted;

    if (tomb_contains(a, id) != 0)
        return 0;                            /* deleted here: nothing to tag */
    noted = katt_lookup(a, id, key, have, sizeof have);
    if (noted == 1 && strcmp(have, ts) >= 0)
        return 0;                            /* already noted, at least this new */
    if (ktomb_lookup(a, id, key, have, sizeof have) == 1 && have[0] != '\0' &&
        strcmp(have, ts) >= 0)
        return 0;                            /* a detach here is newer: it wins */
    /* The lookup above already answered whether there is an entry to replace: a
     * fact this index has never seen appends instead of rewriting the whole file. */
    if ((noted == 1 ? katt_set(a, id, ts, hash, key)
                    : katt_add(a, id, ts, hash, key)) != 0)
        return -1;
    if (ais_post_keys(a, key, id, ts, 0) != 0) {
        /* The attach was NOTED but never reached the authoritative keys field.
         * Left there, katt exports it as a T| and tells every peer this device
         * carries a tag it does not. Retract the note rather than advertise it. */
        katt_forget(a, id, key);
        return -1;
    }
    return 0;
}

/* Resolve a whole batch of attach facts in ONE store pass, then apply in STREAM
 * order -- the shape of ais_merge_del_many (see ais.h). Applying rewrites keys
 * fields and postings, never a VALUE, so the hash->id map stays valid. */
struct abatch_ctx { const ais_att_fact *f; int n, left; long id[AIS_ATT_BATCH]; };
static int abatch_seek(long id, const char *ts, const char *keys, const char *value, void *vp)
{
    struct abatch_ctx *B = vp;
    char h[17];
    int i;

    (void)ts;
    (void)keys;
    content_hash(value, h);
    for (i = 0; i < B->n; i++) {
        if (B->id[i] != 0 || strcmp(h, B->f[i].hash) != 0)
            continue;
        B->id[i] = id;          /* the FIRST matching line, where a single seek stops */
        B->left--;
    }
    return (B->left == 0) ? -1 : 0;      /* nothing left to look for: stop reading */
}

int ais_merge_attach_many(ais *a, const ais_att_fact *facts, int n)
{
    struct abatch_ctx B;
    int i, rc = 0;

    if (a == NULL || facts == NULL || n < 0 || n > AIS_ATT_BATCH)
        return -1;
    if (n == 0)
        return 0;
    B.f = facts;
    B.n = n;
    B.left = n;
    for (i = 0; i < n; i++)
        B.id[i] = 0;                     /* 0 = no local record named by this fact */
    if (store_wlock(a) != 0)
        return -1;
    store_each_record(a, abatch_seek, &B);
    for (i = 0; i < n; i++)
        if (B.id[i] != 0 &&
            att_apply(a, B.id[i], facts[i].hash, facts[i].key, facts[i].ts) != 0)
            rc = -1;
    store_wunlock(a);
    return rc;
}

int ais_merge_attach(ais *a, const char *hash, const char *key, const char *ts)
{
    ais_att_fact f;

    if (a == NULL || hash == NULL || key == NULL || ts == NULL)
        return -1;
    snprintf(f.hash, sizeof f.hash, "%s", hash);
    snprintf(f.key, sizeof f.key, "%s", key);
    snprintf(f.ts, sizeof f.ts, "%s", ts);
    return ais_merge_attach_many(a, &f, 1);
}

/* Detach iff the detach TS is at least as new as the record's own time AND as any
 * attach of that key here (record-granularity LWW, mirroring ais_merge_del).
 * Without the attach time a detach that has reached the mesh once wins for ever: a
 * re-attach is undone on the next sync, even on the device that made it. */
int ais_merge_detach(ais *a, const char *hash, const char *key, const char *ts)
{
    struct mdel_ctx M;
    char att[AIS_TS_MAX];

    if (a == NULL || hash == NULL || key == NULL)
        return -1;
    M.hash = hash;
    M.id = 0;
    M.ts[0] = '\0';
    M.found = 0;
    if (store_wlock(a) != 0)
        return -1;
    store_each_record(a, mdel_seek, &M);
    if (M.found && katt_lookup(a, M.id, key, att, sizeof att) == 1 &&
        att[0] != '\0' && strcmp(att, M.ts) > 0)
        snprintf(M.ts, sizeof M.ts, "%s", att);    /* the key's own time, when later */
    if (M.found && tomb_contains(a, M.id) == 0 &&
        strcmp(ts, M.ts) >= 0 && ktomb_contains(a, M.id, key) == 0) {
        /* The tombstone FIRST, the posting after. The other order lost the key
         * from the posting while the keys field still held it whenever the
         * append failed, and the next compaction read the field and put the tag
         * back -- a removal that undid itself, quietly. */
        if (ktomb_append(a, M.id, ts, hash, key) == 0) {   /* keep + re-propagate */
            post_remove(a, key, M.id);
            katt_forget(a, M.id, key);                     /* no longer attached */
        }
    }
    store_wunlock(a);
    return 0;
}

int ais_get(ais *a, char *const keys[], int nkeys, ais_mode mode,
            ais_id_cb cb, void *ctx)
{
    post_stream streams[AIS_KEYS_MAX];
    int i, opened = 0, rc;

    if (nkeys <= 0)
        return 0;
    if (nkeys > AIS_KEYS_MAX)
        nkeys = AIS_KEYS_MAX;

    for (i = 0; i < nkeys; i++) {
        if (post_open(a, keys[i], &streams[i]) != 0) {
            rc = -1;
            goto cleanup;
        }
        opened++;
    }

    rc = merge_run(a, streams, nkeys, mode, cb, ctx);

cleanup:
    for (i = 0; i < opened; i++)
        post_close(&streams[i]);
    return rc;
}

/* ais_get emits ascending, so the cursor is just the last id shown: skip ids <=
 * AFTER, forward the next COUNT to the caller, then stop the merge. Memory stays
 * O(nkeys) -- the page bound is a counter, not a buffer. */
struct getpage_ctx { long after; int limit, n; ais_id_cb cb; void *ctx; };
static int getpage_cb(long id, void *vp)
{
    struct getpage_ctx *g = vp;
    int rc;
    if (id <= g->after)               /* on/before the cursor: already delivered */
        return 0;
    rc = g->cb(id, g->ctx);           /* hand this row to the caller */
    if (rc != 0)
        return rc;                    /* caller stopped early: propagate */
    return (++g->n >= g->limit) ? 1 : 0;   /* page full: stop the merge */
}

int ais_get_page(ais *a, char *const keys[], int nkeys, ais_mode mode,
                 long after, int count, ais_id_cb cb, void *ctx)
{
    struct getpage_ctx g;
    int rc;
    if (count <= 0)                   /* unbounded == plain ais_get */
        return ais_get(a, keys, nkeys, mode, cb, ctx);
    g.after = after; g.limit = count; g.n = 0; g.cb = cb; g.ctx = ctx;
    rc = ais_get(a, keys, nkeys, mode, getpage_cb, &g);
    return (rc < 0) ? rc : 0;         /* our page-full stop (1) is a normal finish */
}

/* Context for ais_record: forward callback only for the matching id. */
struct record_ctx {
    long        id;
    ais_val_cb  cb;
    void       *ctx;
    int         stop;
};

static int record_seek(long id, const char *ts, const char *keys,
                       const char *value, void *vp)
{
    struct record_ctx *R = vp;
    (void)ts;
    (void)keys;
    if (id == R->id) {
        R->stop = R->cb(id, value, R->ctx);
        return R->stop;   /* nonzero -> stop the scan */
    }
    return 0;
}

int ais_record(ais *a, long id, ais_val_cb cb, void *ctx)
{
    struct record_ctx R;
    long offset;

    /* Fast path: a single-line record whose first-line offset is in "off".
     * Multi-line records (ais_add) and any miss fall through to the scan, and
     * store_value_at re-checks the line's id, so this can never be wrong. */
    if (multi_contains(a, id) == 0 && off_get(a, id, &offset) == 1 &&
        store_value_at(a, id, offset, cb, ctx) == 1)
        return 0;

    R.id = id;
    R.cb = cb;
    R.ctx = ctx;
    R.stop = 0;
    store_each_record(a, record_seek, &R);
    return 0;
}

/* Buffer of distinct key names gathered from idx/, sorted then emitted. Bounded:
 * AIS_KEYS_LIST_MAX names of AIS_KEY_MAX each -- a listing command, not a
 * streaming query. */
#define AIS_KEYS_LIST_MAX 65536

struct key_buf {
    char (*names)[AIS_KEY_MAX];   /* one heap block, freed on every path */
    int    n;
    int    cap;
};

static int key_buf_add(struct key_buf *b, const char *name)
{
    if (b->n >= b->cap)
        return -1;   /* listing exceeds the bounded buffer */
    snprintf(b->names[b->n], AIS_KEY_MAX, "%s", name);
    b->n++;
    return 0;
}

static int key_name_cmp(const void *pa, const void *pb)
{
    return strcmp((const char *)pa, (const char *)pb);
}

int ais_keys(ais *a, ais_key_cb cb, void *ctx)
{
    char idxdir[AIS_PATH_MAX];
    struct key_buf b;
    DIR *idx = NULL;
    struct dirent *pe;
    int i, rc = 0, dead;

    b.names = NULL;
    b.n = 0;
    b.cap = AIS_KEYS_LIST_MAX;

    if (snprintf(idxdir, sizeof(idxdir), "%s/idx", a->dir) >= (int)sizeof(idxdir))
        return -1;
    idx = opendir(idxdir);
    if (idx == NULL)
        return 0;   /* no idx/ yet: no keys */
    dead = tomb_active(a);
    if (dead < 0)
        dead = 1;

    /* one bounded heap block holding the collected key names */
    b.names = malloc((size_t)b.cap * sizeof(b.names[0]));
    if (b.names == NULL) {
        rc = -1;
        goto cleanup;
    }

    /* walk each prefix dir (names of length 1 or 2), collecting its key files */
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
            continue;   /* not a dir (or unreadable): skip, stay local */
        while ((ke = readdir(pd)) != NULL) {
            char kpath[AIS_PATH_MAX];
            if (ke->d_name[0] == '.')
                continue;
            /* Skip a key whose every record is deleted -- the posting keeps their
             * ids until compaction. Same rule as ais_tags; free if nothing is
             * deleted. */
            if (dead && snprintf(kpath, sizeof kpath, "%s/%s", pdir, ke->d_name)
                            < (int)sizeof kpath &&
                tag_count_file(a, kpath, dead) == 0)
                continue;
            if (key_buf_add(&b, ke->d_name) != 0) {
                rc = -1;
                closedir(pd);
                goto cleanup;
            }
        }
        closedir(pd);
    }

    /* sort so output is stable and navigable; emit distinct (skip equal runs) */
    qsort(b.names, (size_t)b.n, sizeof(b.names[0]), key_name_cmp);
    for (i = 0; i < b.n; i++) {
        if (i > 0 && strcmp(b.names[i], b.names[i - 1]) == 0)
            continue;
        rc = cb(b.names[i], ctx);
        if (rc != 0)
            goto cleanup;
    }

cleanup:
    free(b.names);
    if (idx != NULL)
        closedir(idx);
    return rc;
}

/* Write KEYS minus any key detached from ID (per ktomb) into OUT (size OUTSZ).
 * Tokens are space-separated, order preserved; truncates to fit (display use).
 * Only called when the ktomb is active. Returns 0/-1. */
static int keys_visible(ais *a, long id, const char *keys, char *out, size_t outsz)
{
    char buf[AIS_LINE_MAX];
    char *tok, *save;
    size_t used = 0;
    int n;

    if (snprintf(buf, sizeof(buf), "%s", keys) >= (int)sizeof(buf))
        return -1;
    out[0] = '\0';
    for (tok = strtok_r(buf, " \t", &save); tok != NULL;
         tok = strtok_r(NULL, " \t", &save)) {
        int t = ktomb_contains(a, id, tok);
        if (t < 0)
            return -1;
        if (t == 1)
            continue;                          /* detached: hide */
        n = snprintf(out + used, outsz - used, "%s%s", used ? " " : "", tok);
        if (n < 0)
            return -1;
        if (used + (size_t)n >= outsz)
            break;                             /* truncate to fit */
        used += (size_t)n;
    }
    return 0;
}

/* Context for ais_dump: print live store lines (tombstones merged out). */
struct dump_ctx {
    ais  *a;
    FILE *out;
    int   filter;     /* 1 if ktomb active: hide detached keys */
};

static int dump_line(long id, const char *ts, const char *keys,
                     const char *value, void *vp)
{
    struct dump_ctx *D = vp;
    int t = tomb_contains(D->a, id);
    char vis[AIS_LINE_MAX];
    const char *k = keys;
    (void)ts;   /* dump is the CONTENT serialization: the id is a device-local
                 * ordinal and the ts is reassigned on re-import, so neither is
                 * emitted. The line is the CLI's own grammar (FORMAT_V2.md): a
                 * value may contain '|' unescaped, -v taking the rest of the line
                 * with nothing after it parsed. */
    if (t < 0)
        return -1;
    if (t != 0)
        return 0;
    if (D->filter) {
        if (keys_visible(D->a, id, keys, vis, sizeof(vis)) != 0)
            return -1;
        k = vis;
    }
    if (k[0] != '\0')
        fprintf(D->out, "%s -v %s\n", k, value);
    else
        fprintf(D->out, "-v %s\n", value);   /* keyless: --untag leaves these */
    return 0;
}

void ais_dump(ais *a, FILE *out)
{
    struct dump_ctx D;
    D.a = a;
    D.out = out;
    D.filter = (ktomb_active(a) == 1);
    store_each_record(a, dump_line, &D);
}

/* --- timeline: keyset (cursor) pagination, newest id first -----------------
 * With the "off" id->offset index each record is SEEKed by id and only the page
 * asked for is read, never the whole store. One row per record (its first/canonical
 * line). When "off" is absent or stale (a legacy index before --compact) it falls
 * back to a bounded scan -- correct, not scalable. */
#define AIS_TL_DEFAULT  500    /* page size when COUNT <= 0                      */
#define AIS_TL_MAX    10000    /* hard cap on one page (bounds the fallback heap)*/
#define AIS_TL_VAL_MAX 2048    /* value snippet held per row in the fallback     */

/* Forward a record to the caller's cb -- hiding ktomb-detached keys, and only if
 * its date falls in the [from,to] range. Counts the rows actually emitted, so
 * the seek loop pages by in-range records, not by ids scanned. */
struct tl_emit {
    ais_tl_cb   cb;
    void       *ctx;
    ais        *a;
    int         filter;
    const char *from, *to;   /* "YYYY-MM-DD" bounds; "" / NULL = open-ended */
    int         emitted;
};

/* Is TS's date (its first 10 chars) within [FROM,TO]? An empty bound is open; a
 * dateless record (TS == "") is in range only when there is NO bound at all. */
static int tl_in_range(const char *ts, const char *from, const char *to)
{
    char d[11];
    int i;

    if ((from == NULL || from[0] == '\0') && (to == NULL || to[0] == '\0'))
        return 1;                            /* no date filter */
    if (ts[0] == '\0')
        return 0;                            /* dateless: excluded by a range */
    for (i = 0; i < 10 && ts[i] != '\0'; i++)
        d[i] = ts[i];
    d[i] = '\0';
    if (from != NULL && from[0] != '\0' && strcmp(d, from) < 0)
        return 0;
    if (to != NULL && to[0] != '\0' && strcmp(d, to) > 0)
        return 0;
    return 1;
}

static int tl_emit_one(long id, const char *ts, const char *keys,
                       const char *value, void *vp)
{
    struct tl_emit *e = vp;
    char vis[AIS_LINE_MAX];
    const char *k = keys;

    if (!tl_in_range(ts, e->from, e->to))
        return 0;                            /* out of range: skip, do not count */
    if (e->filter) {
        if (keys_visible(e->a, id, keys, vis, sizeof(vis)) != 0)
            return -1;
        k = vis;
    }
    e->emitted++;
    return e->cb(id, ts, k, value, e->ctx);
}

/* One id's line by scan, for when its "off" slot is stale: every other seek
 * user (ais_record, seek_record, del_stamp) already falls back this way, and
 * the timeline silently dropping a record recall still finds is the one
 * failure a memory product cannot afford. */
struct tl_seek {
    struct tl_emit *e;
    long id;
};

static int tl_seek_one(long id, const char *ts, const char *keys,
                       const char *value, void *vp)
{
    struct tl_seek *s = vp;

    if (id != s->id)
        return 0;
    if (tl_emit_one(id, ts, keys, value, s->e) < 0)
        return -1;
    return 1;                                /* first line wins, like ais_record */
}

/* --- fallback (no usable "off"): one bounded scan keeping the COUNT highest
 * live ids below BOUND, one row per id, emitted id-descending. --------------- */
struct tl_entry {
    long id;
    char ts[AIS_TS_MAX];
    char keys[AIS_KEY_MAX];
    char value[AIS_TL_VAL_MAX];
};

struct tl_scan_ctx {
    ais             *a;
    long             bound;   /* keep only ids strictly below this              */
    struct tl_entry *top;     /* the COUNT highest qualifying ids (unsorted)    */
    int              cap, n;
    const char      *from, *to;   /* date range, "" = open                      */
};

static int tl_scan_collect(long id, const char *ts, const char *keys,
                           const char *value, void *vp)
{
    struct tl_scan_ctx *s = vp;
    int i, slot, t;

    if (id >= s->bound)
        return 0;
    if (!tl_in_range(ts, s->from, s->to))
        return 0;                 /* out of the date range */
    t = tomb_contains(s->a, id);
    if (t < 0)
        return -1;
    if (t == 1)
        return 0;                 /* deleted */
    for (i = 0; i < s->n; i++)
        if (s->top[i].id == id)
            return 0;             /* one row per id (keep the first line seen) */

    if (s->n < s->cap) {
        slot = s->n++;
    } else {                      /* full: replace the smallest id, if we beat it */
        slot = 0;
        for (i = 1; i < s->n; i++)
            if (s->top[i].id < s->top[slot].id)
                slot = i;
        if (id <= s->top[slot].id)
            return 0;
    }
    s->top[slot].id = id;
    snprintf(s->top[slot].ts,    sizeof(s->top[slot].ts),    "%s", ts);
    snprintf(s->top[slot].keys,  sizeof(s->top[slot].keys),  "%s", keys);
    snprintf(s->top[slot].value, sizeof(s->top[slot].value), "%s", value);
    return 0;
}

static int tl_id_desc(const void *pa, const void *pb)
{
    const struct tl_entry *a = pa, *b = pb;
    return (a->id < b->id) ? 1 : (a->id > b->id) ? -1 : 0;
}

static int tl_scan(ais *a, long before_id, int count, struct tl_emit *e)
{
    struct tl_scan_ctx s;
    int i, rc = 0;

    s.a = a;
    s.bound = (before_id > 0) ? before_id : a->next_id;   /* next_id > every id */
    s.cap = count;
    s.n = 0;
    s.from = e->from;
    s.to = e->to;
    s.top = malloc((size_t)count * sizeof(s.top[0]));
    if (s.top == NULL)
        return -1;
    if (store_each_record(a, tl_scan_collect, &s) < 0) {
        free(s.top);
        return -1;
    }
    qsort(s.top, (size_t)s.n, sizeof(s.top[0]), tl_id_desc);
    for (i = 0; i < s.n; i++) {
        rc = tl_emit_one(s.top[i].id, s.top[i].ts, s.top[i].keys, s.top[i].value, e);
        if (rc != 0)
            break;
    }
    free(s.top);
    return (rc < 0) ? -1 : 0;
}

int ais_timeline(ais *a, long before_id, int count,
                 const char *from, const char *to, ais_tl_cb cb, void *ctx)
{
    struct tl_emit e;
    long id, maxid;

    if (count <= 0)
        count = AIS_TL_DEFAULT;
    if (count > AIS_TL_MAX)
        count = AIS_TL_MAX;
    e.cb = cb; e.ctx = ctx; e.a = a;
    e.filter = (ktomb_active(a) == 1);
    e.from = from; e.to = to; e.emitted = 0;

    /* Re-read the counter from disk BEFORE anything reads it. It is cached at
     * open, so in a process that stays up -- the web server, the phone app -- every
     * record another writer adds sits above the frozen ceiling, invisible.
     * off_consistent() compares the off file against next_id too, so a stale counter
     * also mis-declares the accelerator inconsistent and sends this down the scan
     * path: the refresh has to come first. */
    if (store_load_next_id(a) != 0)
        return -1;

    if (off_consistent(a) != 1)               /* no usable index: bounded scan */
        return tl_scan(a, before_id, count, &e);

    /* seek path: walk ids downward, reading only the page -- older records are
     * never touched. Counting is by in-range rows (tl_emit_one increments
     * e.emitted only for records the range admits). */
    maxid = a->next_id - 1;
    id = (before_id > 0 && before_id - 1 < maxid) ? before_id - 1 : maxid;
    for (; id >= 1 && e.emitted < count; id--) {
        long offset;
        int served;
        int t = tomb_contains(a, id);
        if (t < 0)
            return -1;
        if (t == 1)
            continue;                          /* deleted */
        if (off_get(a, id, &offset) != 1)
            continue;                          /* gap / sentinel / short off */
        served = store_record_at(a, id, offset, tl_emit_one, &e);
        if (served < 0)
            return -1;
        if (served == 0) {                     /* stale slot: scan, never drop */
            struct tl_seek s;
            s.e = &e;
            s.id = id;
            if (store_each_record(a, tl_seek_one, &s) < -1)
                return -1;
        }
    }
    return 0;
}

/* --- tags: every distinct key with its posting count, busiest first --------- */
struct tag_entry {
    char key[AIS_KEY_MAX];
    long count;
};

/* LIVE postings in one key file idx/<p>/<key>. A posting keeps a deleted record's
 * id until the next compaction, and on a phone there is no CLI, so compaction never
 * runs; a plain line count therefore offers tags that answer nothing. DEAD is the
 * tombstone count: with nothing deleted (the common case) this is a plain newline
 * count, and only an index that has seen deletes pays for the id parse and the tomb
 * lookup. */
static long tag_count_file(const ais *a, const char *path, int dead)
{
    char buf[8192];
    FILE *fp;
    long n = 0, id = 0;
    int  indig = 0;
    size_t r;

    fp = fopen(path, "r");
    if (fp == NULL)
        return 0;
    while ((r = fread(buf, 1, sizeof(buf), fp)) > 0) {
        size_t i;
        for (i = 0; i < r; i++) {
            if (buf[i] >= '0' && buf[i] <= '9') {
                id = id * 10 + (buf[i] - '0');
                indig = 1;
                continue;
            }
            if (buf[i] != '\n')
                continue;
            if (!dead)                          /* nothing deleted: just count */
                n++;
            else if (indig && tomb_contains(a, id) != 1)
                n++;
            id = 0; indig = 0;
        }
    }
    fclose(fp);
    return n;
}

static int tag_cmp(const void *pa, const void *pb)
{
    const struct tag_entry *a = pa, *b = pb;
    if (a->count != b->count)
        return (a->count < b->count) ? 1 : -1;   /* count descending */
    return strcmp(a->key, b->key);               /* ties: key ascending */
}

int ais_tags_page(ais *a, long after_count, const char *after_key, int count,
                  ais_tag_cb cb, void *ctx)
{
    char idxdir[AIS_PATH_MAX];
    struct tag_entry *tags = NULL;
    int ntags = 0, cap = AIS_KEYS_LIST_MAX, i, rc = 0, emitted = 0, dead;
    DIR *idx;
    struct dirent *pe;

    if (snprintf(idxdir, sizeof(idxdir), "%s/idx", a->dir) >= (int)sizeof(idxdir))
        return -1;
    idx = opendir(idxdir);
    if (idx == NULL)
        return 0;             /* no idx/ yet: no tags */
    dead = tomb_active(a);    /* nothing deleted -> skip the per-id liveness check */
    if (dead < 0)
        dead = 1;             /* unreadable tomb: filter rather than over-report */

    tags = malloc((size_t)cap * sizeof(tags[0]));
    if (tags == NULL) {
        closedir(idx);
        return -1;
    }

    /* walk each prefix dir, counting postings per key file */
    while ((pe = readdir(idx)) != NULL) {
        char pdir[AIS_PATH_MAX];
        DIR *pd;
        struct dirent *ke;

        if (pe->d_name[0] == '.')
            continue;
        if (snprintf(pdir, sizeof(pdir), "%s/%s", idxdir, pe->d_name)
                >= (int)sizeof(pdir))
            continue;
        pd = opendir(pdir);
        if (pd == NULL)
            continue;
        while ((ke = readdir(pd)) != NULL) {
            char kpath[AIS_PATH_MAX];
            if (ke->d_name[0] == '.')
                continue;
            if (ntags >= cap) { rc = -1; closedir(pd); goto cleanup; }
            if (snprintf(kpath, sizeof(kpath), "%s/%s", pdir, ke->d_name)
                    >= (int)sizeof(kpath))
                continue;
            snprintf(tags[ntags].key, sizeof(tags[ntags].key), "%s", ke->d_name);
            tags[ntags].count = tag_count_file(a, kpath, dead);
            if (tags[ntags].count == 0)
                continue;                 /* every record under it is deleted */
            ntags++;
        }
        closedir(pd);
    }

    qsort(tags, (size_t)ntags, sizeof(tags[0]), tag_cmp);
    for (i = 0; i < ntags; i++) {
        /* Keyset cursor over the (count desc, key asc) order: skip everything
         * on or before the (AFTER_COUNT, AFTER_KEY) row from the prior page. */
        if (after_key != NULL) {
            long c = tags[i].count;
            if (c > after_count)
                continue;
            if (c == after_count && strcmp(tags[i].key, after_key) <= 0)
                continue;
        }
        rc = cb(tags[i].key, tags[i].count, ctx);
        if (rc != 0)
            break;
        if (count > 0 && ++emitted >= count)   /* page full */
            break;
    }

cleanup:
    free(tags);
    closedir(idx);
    return rc;
}

int ais_tags(ais *a, ais_tag_cb cb, void *ctx)
{
    return ais_tags_page(a, 0, NULL, 0, cb, ctx);   /* whole cloud, no cursor */
}
