/* ais.h -- AIS public API: a plain-text associative index.
 *
 * An INDEX is a directory (see doc/dev/LAYOUT.md). Open it, then put/get/del/etc.
 * All calls use fixed, stack-sized buffers; memory never scales with the data.
 * get() streams matching ids through a callback.
 */
#ifndef AIS_H
#define AIS_H

#include <stdio.h>
#include "common.h"

/* The version this HEADER describes -- what a caller was compiled against.
 * Compare it with ais_version() below, which is what the loaded library actually
 * is; a stale shared library is otherwise invisible. The same compile-time/
 * runtime pair as SQLite (SQLITE_VERSION / sqlite3_libversion) and zlib
 * (ZLIB_VERSION / zlibVersion). */
#ifndef AIS_VERSION
#define AIS_VERSION "0.0.0-dev"           /* the build stamps the real one in */
#endif

/* The version of the library you are actually running. Never NULL; the storage is
 * static and must not be freed. */
const char *ais_version(void);

/* The same, as an integer for comparisons: major*1000000 + minor*1000 + patch, so
 * 0.3.9 is 3009. A non-numeric or missing stamp yields 0. */
long ais_version_number(void);

/* Open handle. Holds only the path, the id counter, and the writer lock.
 * Declare one on the stack:  ais a; ais_open(&a, dir); ... ais_close(&a); */
/* Called with a value whose LAST reference the engine has just dropped, because
 * a delete arrived from another device. The engine stores paths and knows
 * nothing about what they point at, so deciding whether a file goes with the
 * record belongs to the front end: pass ais_doc_discard_cb (doc.h), which
 * destroys what the index made and never touches the user's own files. Unset,
 * nothing is disposed of and the file waits for the next compaction. */
typedef void (*ais_discard_cb)(const char *value, void *ctx);

typedef struct ais {
    char dir[AIS_PATH_MAX];   /* the INDEX directory                         */
    long next_id;             /* next id to assign (monotonic)               */
    int  lock_fd;             /* single-writer advisory lock; -1 if not held */
    int  purge_deletes;       /* ais_compact also forgets the delete FACTS (see
                               * ais_compact_purge); 0 for every other caller  */
    long survivals;           /* records that have beaten an incoming delete on
                               * THIS handle: bumped whenever a merge records a
                               * survival in `sts`. The host sends its stream
                               * BEFORE it reads the peer's, so the news goes out
                               * only in the NEXT exchange and the two devices
                               * genuinely still differ when this round ends.
                               * Sync compares it across a round to say "run it
                               * again". Not persisted. */
    ais_discard_cb discard;   /* see ais_on_discard; NULL = dispose of nothing  */
    void *discard_ctx;
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

/* ais_put_at with the KEY-attach decision on its own clock. TS answers the
 * record-vs-tombstone question; ATTACH_TS is what attaching a key competes
 * against a prior detach with. They differ only when the exporter RAISED the
 * record's timestamp to survive a peer's delete: that raise must not also outrank
 * key tombstones, so the line's true time travels beside it as the C| verb.
 * ais_put_at passes TS for both. */
long ais_put_at_k(ais *a, const char *keys, const char *value, const char *ts,
                  const char *attach_ts);

/* ais_put_at_k with the value lookup already done. The caller HOLDS the write
 * lock and has loaded next_id (store.h); FOUND_ID is what store_find_value
 * would answer -- the id of the first store line holding VALUE, 0 if none does
 * -- or -1 to scan here. The import's batch path (feed.c) resolves a run of
 * records' values in one store pass instead of one pass per record. */
long ais_put_at_k_resolved(ais *a, const char *keys, const char *value, const char *ts,
                           const char *attach_ts, long found_id);

/* Apply an incoming deletion (content HASH, delete-time TS) under last-write-wins:
 * tombstone the local record whose value hashes to HASH iff the delete is at least as
 * new as that record's add-ts and it is not already deleted. No-op if absent. 0/-1. */
int  ais_merge_del(ais *a, const char *hash, const char *ts);

/* Register the disposer for values a merged delete retires (see ais_discard_cb).
 * Every front end should set it right after ais_open, or a document deleted on
 * another device leaves its file here forever -- and this index keeps handing
 * that file back to the peer that deleted it, since an export streams all of
 * blobs/. */
void ais_on_discard(ais *a, ais_discard_cb cb, void *ctx);

/* One delete fact off the wire: the content hash (16 hex digits, content_hash)
 * and the delete time. */
typedef struct { char hash[17]; char ts[AIS_TS_MAX]; } ais_del_fact;

/* How many facts one pass resolves. Fixed at compile time: the caller buffers
 * this many on its stack and flushes, so the merge stays bounded by struct sizes
 * and never by the size of the stream. */
#define AIS_MERGE_BATCH 256

/* Apply up to AIS_MERGE_BATCH delete facts in ONE store pass, each under
 * ais_merge_del's rule and in the order given. Resolving a hash means scanning
 * the store for the value it names, so one scan per fact costs an import
 * O(deletes x records) -- minutes on a phone syncing with a peer that has deleted
 * a lot. 0, or -1 if any fact could not be recorded. */
int  ais_merge_del_many(ais *a, const ais_del_fact *facts, int n);

/* Apply a remote key-detach (K|ts|hash|key): find the record by value-hash and detach
 * KEY under last-write-wins (folder sync I1). Idempotent. Returns 0, or -1 on bad args. */
int  ais_merge_detach(ais *a, const char *hash, const char *key, const char *ts);

/* Apply a remote key-attach (T|ts|hash|key), the mirror of ais_merge_detach: attach
 * KEY to the record whose value hashes to HASH iff TS is strictly newer than any
 * detach of it here. The A| line carries one timestamp for the whole record, so this
 * is the only way a key attached AFTER the record was created can out-rank a detach
 * made in between; without it a detached key can never be re-attached anywhere in
 * the mesh. Idempotent. Returns 0, or -1 on bad args. */
int  ais_merge_attach(ais *a, const char *hash, const char *key, const char *ts);

/* One key-attach fact off the wire: the record's content hash, the key, and when
 * the key went on. */
typedef struct { char hash[17]; char key[AIS_KEY_MAX]; char ts[AIS_TS_MAX]; } ais_att_fact;

/* How many attach facts one pass resolves. Smaller than AIS_MERGE_BATCH because a
 * fact carries a whole key (AIS_KEY_MAX) and the caller buffers these on its stack,
 * inside feed_import_from, which already carries a line buffer under the whole
 * import chain. 32 still turns 32 store passes into one; 64 costs another 17 KB of
 * a budget measured in tens. */
#define AIS_ATT_BATCH 32

/* Apply up to AIS_ATT_BATCH key-attach facts in ONE store pass, each under
 * ais_merge_attach's rule and in the order given. The same shape as
 * ais_merge_del_many: resolving a hash means scanning the store, and a T| is
 * emitted for every key attached after its record was created -- most of them on an
 * index tagged as it grew -- so one scan per fact costs an ordinary bundle
 * O(attaches x records). Applying can rewrite a line's keys field but never its
 * VALUE, so the hash->id map one pass builds stays valid for the whole batch.
 * 0, or -1 on bad arguments. */
int  ais_merge_attach_many(ais *a, const ais_att_fact *facts, int n);

/* Apply an incoming additional link (M|): attach VALUE, at its own time TS, to
 * the record whose first value hashes to HASH. Idempotent; a hash this index does
 * not hold is a no-op. Without it a restore splits every multi-link record into
 * separate records. TS is the fact's wire time: stamped with the local clock
 * instead, a stale bundle replayed after an edit read as newer than the edit,
 * and the edited-away text came back. */
int  ais_merge_addval(ais *a, const char *hash, const char *value, const char *ts);

/* Attach another value/link to an existing record (the multi-link case).
 * Returns 0, -1 if `id` is unknown or deleted, -2 if ANOTHER record already holds
 * VALUE -- refused to keep "a value is identity" true, which put, the merge stream
 * and ais_set_value all rely on. A value THIS record already holds is a no-op,
 * also 0: appended twice it exports as two A| lines and every peer collapses
 * them, leaving the record one link short. The two errors are distinct: the
 * advice differs. */
int  ais_add(ais *a, long id, const char *value);

/* Edit the keys of an existing record (id is the handle, from any "id|value"
 * line). Each bare token in KEYS is attached, each "-key" detached; the record's
 * id and value are unchanged. Returns 0, or -1 if `id` is unknown/deleted. */
int  ais_update(ais *a, long id, const char *keys);

/* Replace record `id`'s VALUE, keeping its id, ts (timeline position) and keys --
 * the in-place value edit (put/del would re-date the record and mint a new id).
 * Rewrites only the store: the ONE line whose id == `id` and whose value exactly
 * equals OLD_VALUE becomes `id|ts|keys|NEW_VALUE`, every other line unchanged
 * (legacy no-ts lines stay legacy), then the "off" accelerator is shifted past
 * the length change rather than dropped, so keyed reads stay fast (a shift that
 * cannot be done removes it and compaction rebuilds it). Returns 0, or -1 on an
 * unknown id, a value that does not match OLD_VALUE (the store is left
 * untouched), or any IO error. Refuses a deleted id, and a NEW_VALUE any record
 * already holds, this one included: a value is identity here (put is idempotent
 * by value scan, tombstones are hash-stamped), so two records sharing one value
 * make a peer collapse them and a later delete of either take both, and one
 * record holding a value twice exports as two A| lines the peer collapses. That
 * refusal is -2, or -3 when the holder is a DELETED record whose line waits for
 * compaction (--compact clears it; editing anyway resurrected the dead record).
 *
 * The edit reaches the peers as E|ts|hash|value, kept in `edits` and exported
 * before the records: a peer still holding the old value replaces it in place,
 * keeping the record's id, other values, tags and key tombstones (MERGE.md). A
 * peer that predates the verb skips the line and keeps the old value. */
int  ais_set_value(ais *a, long id, const char *old_value, const char *new_value);

/* Apply an arriving edit (content HASH of the old value became VALUE at TS) to
 * the first live record holding it, in place. Nothing if none does, if the
 * record was created after TS, or if VALUE already lives in another record.
 * 1 applied, 0 not, -1 error. */
int  ais_merge_edit(ais *a, const char *hash, const char *value, const char *ts);

/* Tombstone a record. Idempotent (deleting an absent id is a no-op).
 * Space is reclaimed later by ais_compact(). Returns 0. */
int  ais_del(ais *a, long id);

/* Tombstone every record currently filed under KEY, by streaming the key's posting
 * list and tombstoning each id (the mechanism ais_del uses). Idempotent; a key with
 * no records is a no-op. A record already tombstoned is re-stamped (so the deletion
 * holds as of now against a peer add dated in between) but NOT counted -- the count
 * is live records only, so a caller's preview and this return value agree. Returns
 * the number of records tombstoned (>= 0), or -1 on error. */
int  ais_del_key(ais *a, const char *key);

/* Detach KEY from every record filed under it, destroying NOTHING: each record
 * keeps its id, value and other keys, and stops being filed under KEY. The
 * non-destructive counterpart to ais_del_key. Reversible by re-attaching (the same
 * ktomb clearing ais_update does), and it propagates to peers as a K| line.
 * Idempotent; a key with no records is a no-op. KEY is folded the way the posting
 * names it (key_encode), so "a b" and "a_b" address the same tag; NULL or "" is
 * rejected. Already-tombstoned records are skipped, not counted -- they keep no tag
 * to lose, but their detach is still recorded, or resurrecting one would bring the
 * key back at the next compaction; a posting entry naming an id with no store line
 * is simply pruned. Returns the number of records untagged (>= 0), or -1 on error.
 *
 * COST is quadratic in the number of records under KEY: every detach goes through
 * ais_update, which rewrites the key's posting. ~2s for 5k records, ~30s for 20k.
 * The price of ONE detach implementation rather than a second; this is an
 * administrative command behind a confirmation prompt. */
int  ais_untag_key(ais *a, const char *key);

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

/* Keyset page of ais_get, for GUIs that scroll a large match set instead of
 * loading it whole: up to COUNT ids with id > AFTER, ascending (same order and
 * filtering as ais_get). AFTER <= 0 starts from the first match; COUNT <= 0 is
 * unbounded (== ais_get). The next page's AFTER is the last id emitted. Memory
 * stays O(nkeys). Returns 0, or -1 on error. */
int  ais_get_page(ais *a, char *const keys[], int nkeys, ais_mode mode,
                  long after, int count, ais_id_cb cb, void *ctx);

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
 * code. Buffers the key names (bounded by AIS_KEY_MAX each) to sort them. */
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
 * dateless record drops out of any bounded range. Keyset pagination: "load more"
 * passes the last id shown as the next BEFORE_ID (FROM/TO held constant), so a page
 * is read by seeking, not by scanning the whole store. Order is id-descending
 * (~ reverse-chronological). Returns 0, or -1 on error. */
int ais_timeline(ais *a, long before_id, int count,
                 const char *from, const char *to, ais_tl_cb cb, void *ctx);

/* Callback for ais_tags(): one distinct key and how many records are filed
 * under it (its posting count). Return 0 to continue, negative to stop. */
typedef int (*ais_tag_cb)(const char *key, long count, void *ctx);

/* Emit every distinct key with its record count, busiest first (ties: key
 * ascending) -- a plain-list "tag cloud". Counts are LIVE records: a deleted
 * record stops counting at once, without waiting for ais_compact to remove its
 * posting, and a key whose every record is deleted is not emitted at all.
 * Returns 0, the callback's stop code, or -1 on error. */
int ais_tags(ais *a, ais_tag_cb cb, void *ctx);

/* Keyset page of ais_tags, for scrolling a large tag cloud. Emits up to COUNT
 * tags that fall strictly after the (AFTER_COUNT, AFTER_KEY) cursor in the
 * busiest-first order. Pass AFTER_KEY == NULL for the first page; the next
 * page's cursor is the last (count, key) shown. COUNT <= 0 is unbounded.
 * Returns 0, the callback's stop code, or -1 on error. */
int ais_tags_page(ais *a, long after_count, const char *after_key, int count,
                  ais_tag_cb cb, void *ctx);

/* Reclaim space: streaming rewrite of the store dropping tombstoned records and
 * rebuilding the posting index. Tombstones are KEPT: they are the portable delete
 * fact a peer needs, so collecting them would let any device that still holds the
 * record push it back. Returns 0 on success. */
int  ais_compact(ais *a);

/* Compact, and also FORGET what was deleted: each tombstone keeps its id (so the
 * record stays suppressed here) but loses its content hash, which is the part
 * that travels to peers and the part someone holding your files could test a
 * guess against. Deletion becomes final on this device.
 *
 * THE PRICE, and the caller must say it out loud: a device that has not synced
 * since those deletions can push the records back, this index no longer being able
 * to tell it they were deleted. Sync everything first. Returns 0/-1. */
int  ais_compact_purge(ais *a);

#endif /* AIS_H */
