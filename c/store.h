/* store.h -- the append-only store and the writer lock.
 *
 * INDEX/store holds one record line per write:  id|ts|keys|value
 * ts is the save time ("YYYY-MM-DDThh:mm:ss", local). ids are monotonic, so the
 * file is physically in id order. A record may span several lines sharing one
 * id (multi-link; see ais_add). The store is the source of truth and the
 * value->id map (idempotent put scans it). Legacy v1 lines (id|keys|value, no
 * ts) still parse -- their ts comes back empty.
 *
 * INDEX/next_id caches the next id; if missing it is recovered by one
 * streaming pass taking max(id)+1. Reads take no lock; writers take an
 * exclusive flock on INDEX/lock for the duration of one mutating op.
 *
 * Modules return 0/-1 (or a value/-1); only main.c turns errors into die().
 */
#ifndef AIS_STORE_H
#define AIS_STORE_H

#include "ais.h"

/* Open (creating if absent) the INDEX dir A->dir, open the lock file (but take
 * NO lock -- reads are lock-free), and load/recover A->next_id. Returns 0, or
 * -1 on error (dir cannot be made, ...). */
int store_open(ais *a, const char *dir);

/* Close the handle, releasing the lock fd. No lock is held between ops and
 * next_id is saved by each write, so close does not persist next_id. */
void store_close(ais *a);

/* Writer lock for ONE mutating op: store_wlock takes an exclusive flock
 * (blocking -- a second writer waits), store_wunlock releases it. Reads never
 * lock. wlock returns 0/-1. */
int  store_wlock(ais *a);
void store_wunlock(ais *a);

/* (Re)load A->next_id from disk (recovering from the store if the cache is
 * absent). Writers call this under the lock so two processes never assign the
 * same id. Returns 0/-1. */
int  store_load_next_id(ais *a);

/* Persist A->next_id to INDEX/next_id. Returns 0 on success, -1 on error. */
int store_save_next_id(const ais *a);

/* Write INDEX/version = the current AIS_FORMAT_VERSION. Returns 0/-1.
 * store_open stamps a new/legacy index; compact refreshes it. */
int store_write_version(const ais *a);

/* Format the current local time as "YYYY-MM-DDThh:mm:ss" into BUF (size >=
 * AIS_TS_MAX). Sets BUF to "" and returns -1 if the clock cannot be read. */
int store_now(char *buf, size_t bufsz);

/* Append one record line "id|ts|keys|value\n" to INDEX/store (a "" ts falls
 * back to the legacy "id|keys|value" form). Returns 0 on success, -1 on error. */
int store_append(const ais *a, long id, const char *ts,
                 const char *keys, const char *value);

/* Scan the store for a line whose value field exactly equals VALUE.
 * On match, store its id in *out_id and return 1. Return 0 if not found,
 * -1 on error. (The store IS the value->id map; this is the idempotency scan.) */
int store_find_value(const ais *a, const char *value, long *out_id);

/* Callback for store_each_record(): one parsed store line. TS, KEYS and VALUE
 * point into a bounded line buffer valid only for the call (TS is "" for a
 * legacy v1 line). Return 0 to continue, negative to stop early. */
typedef int (*store_rec_cb)(long id, const char *ts, const char *keys,
                            const char *value, void *ctx);

/* Stream every store line in order through CB. Returns 0, the callback's stop
 * code, or -1 on error. */
int store_each_record(const ais *a, store_rec_cb cb, void *ctx);

/* Recompute next_id = max(id in store) + 1 by one streaming pass. Returns the
 * value (>= 1) on success, -1 on error. */
long store_recover_next_id(const ais *a);

/* --- record fast path: the id->offset index ("off") and multi-line set -----
 * Pure accelerators, rebuildable from the store. ais_record falls back to a
 * full scan whenever they are absent, stale, or the id is multi-line, so they
 * can never return wrong data (store_value_at re-checks the line's id). */

/* Byte size of the store (the offset a new line would be appended at).
 * 0 if the store does not exist yet; -1 on error. */
long store_bytes(const ais *a);

/* Write one "off" entry to an OPEN handle (offset < 0 => the absent sentinel).
 * compact uses this to rebuild "off" in one pass. */
void off_write(FILE *fp, long offset);

/* Append one "off" entry to INDEX/off (offset < 0 => sentinel). 0 or -1. */
int  off_append(const ais *a, long offset);

/* Is INDEX/off exactly (next_id-1) entries long, i.e. safe to append to? A
 * fresh index (no records) is consistent; a pre-"off" index is not. 1/0. */
int  off_consistent(const ais *a);

/* Look up id's first-line offset in INDEX/off. 1 + sets *offset; 0 if absent
 * (sentinel / short file / no off); -1 on error. */
int  off_get(const ais *a, long id, long *offset);

/* Emit the value of the store line at byte OFFSET, but only if that line's id
 * equals ID (else the offset is stale). 1 served, 0 mismatch/empty, -1 error. */
int  store_value_at(const ais *a, long id, long offset, ais_val_cb cb, void *ctx);

/* Parse the WHOLE record (id|ts|keys|value) at byte OFFSET and forward it to CB,
 * but only if that line's id equals ID. For paging by id (timeline) without a
 * scan. 1 served, 0 mismatch/stale, -1 error. */
int  store_record_at(const ais *a, long id, long offset, store_rec_cb cb, void *ctx);

/* The "multi" set: ids carrying more than one value line (ais_add). */
int  multi_append(const ais *a, long id);
int  multi_contains(const ais *a, long id);   /* 1 yes, 0 no, -1 error */

/* Stable content hash of a record's "keys|value" identity (FNV-1a 64-bit, hex,
 * 16 chars + NUL). A record's cross-device identity for merge; NOT a security
 * hash. Same content -> same hash on any device, independent of local ids. */
void content_hash(const char *keys, const char *value, char out[17]);

#endif /* AIS_STORE_H */
