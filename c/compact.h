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
int tomb_active(const ais *a);    /* 1 if anything is deleted, 0 if not, -1 */

/* When each record was last edited HERE, one fixed-width slot per id in "mts".
 * LOCAL ONLY: the exported timestamp also decides key-attach conflicts, so
 * raising it would resurrect tags other devices removed. Merging compares
 * mts_effective(); a record that survives a delete is restamped, which is how
 * that decision reaches the other devices. */
int  mts_set(const ais *a, long id, const char *ts);
int  mts_clear(const ais *a, long id);                          /* delete is delete */
int  mts_get(const ais *a, long id, char *out, size_t outsz);   /* 1 found / 0 / -1 */
void mts_effective(const ais *a, long id, const char *add_ts, char *out, size_t outsz);
int  mts_forget_dead(const ais *a);   /* compaction: blank the deleted ids' slots */

/* "sts": the time a record was restamped to after SURVIVING a peer's delete. Kept
 * out of the store line (there it makes a K| detach's outcome depend on the order
 * peer bundles are read in) and applied only by the export, which is what the
 * deleting peer needs in order to converge. */
int  sts_active(const ais *a);        /* 1 if any record has survived a delete */
int  sts_set(const ais *a, long id, const char *ts);
int  sts_clear(const ais *a, long id);
int  sts_get(const ais *a, long id, char *out, size_t outsz);
void sts_effective(const ais *a, long id, const char *line_ts, char *out, size_t outsz);

/* Undo a compaction that was killed mid-flight (see the comment on the
 * definition). Call once at open. 1 = recovered, 0 = nothing to do, -1 = error. */
int compact_recover(ais *a);

/* Stream each tomb entry (id, ts, hash) through CB (ts/hash "" for a legacy v1
 * entry). Returns 0, the callback's stop code, or -1 on error. */
typedef int (*tomb_cb)(long id, const char *ts, const char *hash, void *ctx);
int tomb_each(const ais *a, tomb_cb cb, void *ctx);

/* Copy id's delete-ts into TS ("" for a legacy entry). 1 tombstoned / 0 / -1. */
int tomb_lookup(const ais *a, long id, char *ts, size_t tsz);

/* Remove every tomb entry for ID (rewrite) -- resurrect the record. 0/-1. */
int tomb_remove(const ais *a, long id);
int tomb_lookup_hash(const ais *a, const char *hash, char *ts, size_t tsz, long *idp);
int tomb_remove_hash(const ais *a, const char *hash);

/* Key-level tombstones (INDEX/ktomb): "record ID does not carry KEY", the
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

/* Key-level ATTACH times (INDEX/katt): "record ID gained KEY at TS", ktomb's
 * mirror image, same "id|ts|hash|key" lines and the same accessors. The A| line
 * carries only the record's own timestamp, which cannot express a key attached
 * later, so without katt a key any device had detached could never be re-attached.
 * An entry is written only for an attach to an ALREADY-EXISTING record (a key
 * attached at creation is already in the record's keys field at the record's own
 * time), travels as K|'s counterpart T|<ts>|<hash>|<key>, and is what an incoming
 * K| must beat to remove a key.
 *
 * One entry per (id,key) -- the time it was last attached, not a log -- so
 * katt_set replaces. Detaching drops it, deleting the record drops all of them
 * (delete is delete), and compaction sweeps whatever those two missed. */
int katt_set(const ais *a, long id, const char *ts, const char *hash, const char *key);

/* katt_set for a pair the caller has just looked up and knows is NOT recorded:
 * appends, rather than rewriting the file to drop an old entry that cannot exist.
 * On a first import, where every arriving attach is new, a rewrite each is
 * quadratic in the number of tags. Wrong to call when the pair IS present: it
 * would leave two entries for it. */
int katt_add(const ais *a, long id, const char *ts, const char *hash, const char *key);
int katt_lookup(const ais *a, long id, const char *key, char *ts, size_t tsz); /* 1/0/-1 */
int katt_each(const ais *a, ktomb_cb cb, void *ctx);
int katt_forget(const ais *a, long id, const char *key);  /* KEY, or all of ID if NULL */
int katt_active(const ais *a);            /* 1 if katt has entries, 0 if not, -1 */

/* Streaming compaction. Returns 0 on success, -1 on error. */
int ais_compact(ais *a);

#endif /* AIS_COMPACT_H */
