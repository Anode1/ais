/* ais.h -- AIS public API: a plain-text associative index.
 *
 * An INDEX is a directory (see doc/dev/LAYOUT.md). Open it, then put/get/del/etc.
 * All calls use fixed, stack-sized buffers; memory never scales with the data.
 * get() streams matching ids through a callback, so a query over a 10 GB store
 * costs the same memory as over a 10 KB one.
 */
#ifndef AIS_H
#define AIS_H

#include <stdio.h>
#include "common.h"

/* Open handle. Holds only the path, the id counter, and the writer lock.
 * Declare one on the stack:  ais a; ais_open(&a, dir); ... ais_close(&a); */
typedef struct ais {
    char dir[AIS_PATH_MAX];   /* the INDEX directory                         */
    long next_id;             /* next id to assign (monotonic)               */
    int  lock_fd;             /* single-writer advisory lock; -1 if not held */
} ais;

/* Open (creating if absent) the INDEX directory `dir`, taking a single-writer
 * advisory lock for the lifetime of the handle.
 * Returns 0 on success, -1 on error (including the lock being held). */
int  ais_open(ais *a, const char *dir);

/* Release the lock and flush the id counter. */
void ais_close(ais *a);

/* Put VALUE under one or more whitespace-separated KEYS.
 * Idempotent on VALUE: if VALUE is already stored, its existing record is
 * reused (and any new keys added to it); identical re-puts change nothing.
 * Returns the record id (> 0), or -1 on error. */
long ais_put(ais *a, const char *keys, const char *value);

/* Like ais_put, but stamp a NEW record with TS (NULL = now), and if the value exists
 * but is tombstoned, resurrect it only when TS is newer than the deletion (last-write-
 * wins; NULL/now always wins). The merge primitive shared by put and --import. */
long ais_put_at(ais *a, const char *keys, const char *value, const char *ts);

/* Apply an incoming deletion (content HASH, delete-time TS) under last-write-wins:
 * tombstone the local record whose value hashes to HASH iff the delete is at least as
 * new as that record's add-ts and it is not already deleted. No-op if absent. 0/-1. */
int  ais_merge_del(ais *a, const char *hash, const char *ts);

/* Apply a remote key-detach (K|ts|hash|key): find the record by value-hash and detach
 * KEY under last-write-wins (folder sync I1). Idempotent. Returns 0, or -1 on bad args. */
int  ais_merge_detach(ais *a, const char *hash, const char *key, const char *ts);

/* Attach another value/link to an existing record (the multi-link case).
 * Returns 0 on success, -1 if `id` is unknown. */
int  ais_add(ais *a, long id, const char *value);

/* Edit the keys of an existing record (id is the handle, from any "id|value"
 * line). Each bare token in KEYS is attached, each "-key" detached; the record's
 * id and value are unchanged. Returns 0, or -1 if `id` is unknown/deleted. */
int  ais_update(ais *a, long id, const char *keys);

/* Replace record `id`'s VALUE, preserving its id, ts (timeline position) and
 * keys -- the in-place value edit (put/del would re-date the record and mint a
 * new id). Rewrites only the store: the ONE line whose id == `id` and whose
 * value exactly equals OLD_VALUE becomes `id|ts|keys|NEW_VALUE`, every other
 * line unchanged (legacy no-ts lines stay legacy), then the stale "off"
 * accelerator is dropped so it rebuilds lazily. Returns 0 on success, or -1 on
 * an unknown id, a value that does not match OLD_VALUE (the store is left
 * untouched), or any IO error. */
int  ais_set_value(ais *a, long id, const char *old_value, const char *new_value);

/* Tombstone a record. Idempotent (deleting an absent id is a no-op).
 * Space is reclaimed later by ais_compact(). Returns 0. */
int  ais_del(ais *a, long id);

/* Tombstone every record currently filed under KEY, by streaming the key's
 * posting list and tombstoning each id (the same mechanism ais_del uses).
 * Idempotent; a key with no records is a no-op. Returns the number of records
 * tombstoned (>= 0), or -1 on error. */
int  ais_del_key(ais *a, const char *key);

/* Retrieval mode for ais_get(). */
typedef enum { AIS_AND, AIS_OR } ais_mode;

/* Callback for ais_get(): receives each surviving id. Return 0 to continue,
 * negative to stop early (ais_get then returns that value). */
typedef int (*ais_id_cb)(long id, void *ctx);

/* Get records filed under the given keys, as a streaming k-way merge over the
 * keys' sorted posting lists. AIS_AND = intersection, AIS_OR = union.
 * Each surviving id is emitted once, ascending, tombstones merged out.
 * Memory is O(nkeys). Returns 0, or the callback's stop code. */
int  ais_get(ais *a, char *const keys[], int nkeys, ais_mode mode,
             ais_id_cb cb, void *ctx);

/* Callback for ais_record(): receives each value/link of one record. */
typedef int (*ais_val_cb)(long id, const char *value, void *ctx);

/* Resolve a record by id, emitting each of its values (a record may hold
 * several links). Bounded line buffer; forward scan. Returns 0. */
int  ais_record(ais *a, long id, ais_val_cb cb, void *ctx);

/* Callback for ais_keys(): receives each distinct key. Return 0 to continue,
 * negative to stop early (ais_keys then returns that value). */
typedef int (*ais_key_cb)(const char *key, void *ctx);

/* Emit every DISTINCT key once, in ascending (sorted) order, via CB. Keys are
 * the filenames under idx/<p>/<key>; the walk covers all prefix dirs. If idx/
 * is absent, emits nothing and returns 0. Returns 0, or the callback's stop
 * code. Buffers the key names (bounded by AIS_KEY_MAX each) to sort them --
 * acceptable for a listing command, keys are not astronomically many. */
int  ais_keys(ais *a, ais_key_cb cb, void *ctx);

/* Stream every live record (tombstones merged out) to `out`, one per line. */
void ais_dump(ais *a, FILE *out);

/* Callback for ais_timeline(): one live record line. TS is its save time
 * ("YYYY-MM-DDThh:mm:ss") or "" if it has none. Return 0 to continue. */
typedef int (*ais_tl_cb)(long id, const char *ts, const char *keys,
                         const char *value, void *ctx);

/* Emit one timeline page: the COUNT live records with id < BEFORE_ID (BEFORE_ID
 * <= 0 = from the newest; COUNT <= 0 = a default), newest id first, one row per
 * record, restricted to those whose save date is within [FROM,TO]. FROM and TO
 * are "YYYY-MM-DD" (inclusive by day); either "" / NULL is open-ended, and a
 * dateless record drops out of any bounded range. Keyset pagination -- "load
 * more" passes the last id shown as the next BEFORE_ID (FROM/TO held constant),
 * so each page is read by seeking, not by scanning the whole store. Order is
 * id-descending (~ reverse-chronological). Returns 0, or -1 on error. */
int ais_timeline(ais *a, long before_id, int count,
                 const char *from, const char *to, ais_tl_cb cb, void *ctx);

/* Callback for ais_tags(): one distinct key and how many records are filed
 * under it (its posting count). Return 0 to continue, negative to stop. */
typedef int (*ais_tag_cb)(const char *key, long count, void *ctx);

/* Emit every distinct key with its record count, busiest first (ties: key
 * ascending) -- a plain-list "tag cloud". Counts are postings as filed; a
 * deleted-but-not-yet-compacted record still counts until ais_compact runs.
 * Returns 0, the callback's stop code, or -1 on error. */
int ais_tags(ais *a, ais_tag_cb cb, void *ctx);

/* Reclaim space: streaming rewrite of the store dropping tombstoned records,
 * rebuild the posting index, clear the tombstone log. Returns 0 on success. */
int  ais_compact(ais *a);

#endif /* AIS_H */
