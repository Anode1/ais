# Seamless index merging — design note (IMPLEMENTED)

Goal: merge two AIS indexes (two devices, or two copies of one) into the union of records,
de-duplicated, with **deletions propagating**, deterministically. This is the engine
foundation under built-in sync (`doc/dev/SYNC_PROTOCOL.md`) and the manual merge case.

Status: built and tested in `c/` (`content_hash`, tomb v2 `id|ts|hash`, `feed_export`,
merge-aware `feed_import`, `ais_put_at`, `ais_merge_del`; round-trip test in `tests.c`).
As-built refinements vs the original draft: record identity is the **value** (not
keys+value); the content hash is **FNV-1a** of the value (not blake2b); **no migration**
(v1 `id`-only tomb lines coexist with v2). Deferred follow-ups: multi-value (`ais_add`)
grouping across a merge, and key-level (`ktomb`) removals.

## The core problem: ids are device-local
`store` ids are monotonic PER INDEX. Device A's id 5 and device B's id 5 are different
records. So the existing `tomb` (a list of deleted *ids*) is meaningless across devices:
"B deleted id 7" cannot be applied to A. A cross-device merge must key on something stable
across devices, and must carry timestamps to resolve add-vs-delete conflicts.

## Identity = content; conflict = last-write-wins by ts
- **Identity.** A record's cross-device identity is its **value** (as-built: `put` dedups by
  value via `store_find_value`, reusing the id and attaching keys, so the same value IS the
  same record and keys are labels that union on merge). The content hash is of the value
  alone. No UUID column (rejected: heavier, and it would change idempotent-put semantics).
- **Conflict.** Resolve with **last-write-wins by the store's `ts` column**: compare the
  latest ADD ts against the latest DELETE ts for that content. Delete-after-add removes it;
  re-add-after-delete keeps it. Deterministic for a single user across their own devices.

## What this needs on disk: timestamped, content-addressed tombstones
Today `tomb` is `id` per line: local-only and untimestamped, so neither portable nor
conflict-resolvable. Change it to:

    tomb (v2):   <id>|<ts>|<hash>     # hash = a stable content hash of "keys|value"

- **id** keeps today's fast LOCAL suppression unchanged (`tomb_contains` still matches by id).
- **ts** (the delete time) enables last-write-wins.
- **hash** makes the deletion PORTABLE and compaction-proof: it is emitted as the
  cross-device delete fact (`D|ts|hash`) and survives even after compaction physically
  drops the deleted store line.

Hash: a fast NON-crypto hash (FNV-1a 64-bit, hex) of the record's **value** (its identity). It is
content-addressing, not a security boundary, so the core engine needs no crypto dependency
(the AEAD that protects the wire lives in `sync.c`, separately). Collision risk is
negligible at personal scale.

Local `del(id)`: resolve the record's content, compute the hash, append `id|<now>|<hash>`.
Read-time suppression stays id-keyed (fast), comparing the stored delete-ts against the
record's add-ts so a re-add after a delete reappears.

## Two tombstone types (both must merge)
There are **two** delete mechanisms today, both id-keyed and untimestamped:
- `tomb` — whole-record deletion (`del`, `del-key` cascade).
- `ktomb` — per-key removal (`del_key` strips one key from a record that otherwise stays).
Record-level (`tomb`) is the common case and covers v1. Key-level (`ktomb`) needs the same
content-addressing + ts treatment: a portable fact `<ts>|<record-hash>|<key>`. DECISION:
`ktomb` is a **follow-up**; v1 ships record-level merge only, key-removals get the same
content-addressed treatment next.

## Merge algorithm (content-keyed, ts-resolved)
Given local index A and incoming index B:

    1. Stream B.store  -> for each record, key = content_hash(value), event = (ADD, ts).
    2. Stream B.tomb   -> for each, event = (DEL, ts) under that hash.
    3. Per hash, take the MAX-ts event across A's own state and B's (A already knows its own).
    4. Apply to A:
         winner ADD, not already present -> put the record (idempotent).
         winner DEL                       -> ensure (ts,hash) in A.tomb; record stays suppressed.
    5. Rebuild idx/off (derived, as today).

Symmetric: run both directions (A pulls B, then B pulls A) and both converge to the same
live set. `--import` grows to understand DEL events (today it only adds).

## Export-wire format (so deletions travel)
DECISION: plain `--dump` stays **unchanged** — human-readable and greppable, live records
only. The prefixed line types below are the **export-wire** format (what `--export` serves
and `--import <url>` consumes), NOT `--dump` output:

    A|<ts>|<keys>|<value>      # add  (a live record)
    D|<ts>|<hash>              # delete (a content-addressed tombstone)

`--import` applies `A` lines via `put` and `D` lines via the tomb/suppress path, both under
last-write-wins. A plain (unprefixed) line fed to `--import` from a file/stdin is treated as
an `A` line with ts unknown (oldest), so a hand-edited or legacy dump still imports as adds.

## Migration
- `version` bump. `tomb` v1 (`id`) -> v2 (`ts|hash`): on open, resolve each tombstoned id to
  its content via the store and rewrite as `0|hash` (ts=0 = "deleted long ago"; loses to any
  real re-add, the safe default). Ids already compacted away cannot be recovered, acceptable,
  they are physically gone. `store_open` already does in-place v1->v2 for the store; extend it.

## Edge cases / assumptions
- **Clock skew.** Last-write-wins assumes roughly-synced clocks (NTP-normal). A badly skewed
  device could mis-order a delete vs a re-add. Acceptable for single-user; Lamport/vector
  clocks are the heavier fix if ever needed (out of scope).
- **Content collision.** Two different notes with identical `(keys,value)` are one logical
  record by design, same as today's idempotent put. Acceptable.
- **Edit = del + add** at the content level; nothing special.
- **Hash width.** blake2b-128 (truncated) is ample at personal scale; not a security
  boundary (content is already content-addressed). Confirm width in review.

## Open questions
1. RESOLVED: `ktomb` (key-level) is a follow-up; v1 = record-level only.
2. Tomb hash width: 128-bit truncated vs full 256-bit.
3. RESOLVED: plain `--dump` stays unchanged (readable); the prefixed `A|`/`D|` lines are the
   export-wire format only (see "Export-wire format" above).
4. Any existing consumer that parses `tomb` or `--dump` output? (grep before changing format.)
5. Store `keys` alongside the hash in `tomb` for a human-readable tombstone, or hash-only?
