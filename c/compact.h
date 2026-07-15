/* compact.h -- tombstones and compaction.
 *
 * del(id) appends an id to INDEX/tomb; get/dump merge it out. Physical removal
 * happens only at compaction, which streams the store dropping tombstoned ids
 * into store.new, rebuilds idx/, renames atomically, clears tomb, and
 * recomputes next_id. Bounded buffers throughout.
 */
#ifndef AIS_COMPACT_H
#define AIS_COMPACT_H

#include "ais.h"

/* Tombstone ID into INDEX/tomb as "id|ts|hash" (v2; ts/hash may be ""). Legacy
 * "id"-only lines still read. Returns 0, or -1 on error. */
int tomb_append(const ais *a, long id, const char *ts, const char *hash);

/* Is ID tombstoned? Streams INDEX/tomb. Returns 1 yes, 0 no, -1 on error.
 * Bounded memory; O(tomb) per call. */
int tomb_contains(const ais *a, long id);

/* Stream each tomb entry (id, ts, hash) through CB (ts/hash "" for a legacy v1
 * entry). Returns 0, the callback's stop code, or -1 on error. */
typedef int (*tomb_cb)(long id, const char *ts, const char *hash, void *ctx);
int tomb_each(const ais *a, tomb_cb cb, void *ctx);

/* Copy id's delete-ts into TS ("" for a legacy entry). 1 tombstoned / 0 / -1. */
int tomb_lookup(const ais *a, long id, char *ts, size_t tsz);

/* Remove every tomb entry for ID (rewrite) -- resurrect the record. 0/-1. */
int tomb_remove(const ais *a, long id);

/* Key-level tombstones (INDEX/ktomb): "record ID no longer carries KEY", the
 * append-only counterpart of the record tomb. Detach records the pair here (with a
 * ts + the record's value-hash, its cross-device identity) and drops the posting;
 * dump/timeline hide the key; compaction strips it from the store line but RETAINS
 * hash-bearing entries so the detach can propagate (folder sync I1). Line format:
 * "id|ts|hash|key" (legacy "id|key" still reads). Re-attaching the key removes it. */
int ktomb_append(const ais *a, long id, const char *ts, const char *hash, const char *key);
int ktomb_remove(const ais *a, long id, const char *key);   /* drop (id,key); 0/-1 */
int ktomb_contains(const ais *a, long id, const char *key); /* 1 yes / 0 no / -1 */
int ktomb_lookup(const ais *a, long id, const char *key, char *ts, size_t tsz); /* +ts */
int ktomb_active(const ais *a);   /* 1 if ktomb has entries, 0 if empty/absent, -1 */

/* Stream each ktomb entry (id, ts, hash, key) through CB (ts/hash "" for a legacy
 * entry). For the merge export of key-detaches. 0, the stop code, or -1. */
typedef int (*ktomb_cb)(long id, const char *ts, const char *hash, const char *key, void *ctx);
int ktomb_each(const ais *a, ktomb_cb cb, void *ctx);

/* Streaming compaction. Returns 0 on success, -1 on error. */
int ais_compact(ais *a);

#endif /* AIS_COMPACT_H */
