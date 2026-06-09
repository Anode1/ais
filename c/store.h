/* store.h -- the append-only store and the writer lock.
 *
 * INDEX/store holds one record line per write:  id|keys|value
 * ids are monotonic, so the file is physically in id order. A record may span
 * several lines sharing one id (multi-link; see ais_add). The store is the
 * source of truth and the value->id map (idempotent put scans it).
 *
 * INDEX/next_id caches the next id; if missing it is recovered by one
 * streaming pass taking max(id)+1. INDEX/lock holds a single-writer flock for
 * the lifetime of the open handle.
 *
 * Modules return 0/-1 (or a value/-1); only main.c turns errors into die().
 */
#ifndef AIS_STORE_H
#define AIS_STORE_H

#include "ais.h"

/* Open (creating if absent) the INDEX dir A->dir, take the single-writer
 * flock on INDEX/lock, and load/recover A->next_id. Returns 0, or -1 on error
 * (dir cannot be made, lock already held, ...). */
int store_open(ais *a, const char *dir);

/* Release the lock and flush next_id to disk. */
void store_close(ais *a);

/* Persist A->next_id to INDEX/next_id. Returns 0 on success, -1 on error. */
int store_save_next_id(const ais *a);

/* Append one record line "id|keys|value\n" to INDEX/store.
 * Returns 0 on success, -1 on error. */
int store_append(const ais *a, long id, const char *keys, const char *value);

/* Scan the store for a line whose value field exactly equals VALUE.
 * On match, store its id in *out_id and return 1. Return 0 if not found,
 * -1 on error. (The store IS the value->id map; this is the idempotency scan.) */
int store_find_value(const ais *a, const char *value, long *out_id);

/* Callback for store_each_record(): one parsed store line. KEYS and VALUE
 * point into a bounded line buffer valid only for the call. Return 0 to
 * continue, negative to stop early. */
typedef int (*store_rec_cb)(long id, const char *keys, const char *value,
                            void *ctx);

/* Stream every store line in order through CB. Returns 0, the callback's stop
 * code, or -1 on error. */
int store_each_record(const ais *a, store_rec_cb cb, void *ctx);

/* Recompute next_id = max(id in store) + 1 by one streaming pass. Returns the
 * value (>= 1) on success, -1 on error. */
long store_recover_next_id(const ais *a);

#endif /* AIS_STORE_H */
