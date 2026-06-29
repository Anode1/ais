# Seamless index merging — design note (DRAFT, for review)

Goal: merge two AIS indexes (two devices, or two copies of one) into the union of records,
de-duplicated, with **deletions propagating**, deterministically. This is the engine
foundation under built-in sync (`doc/dev/SYNC_PROTOCOL.md`) and the manual merge case.
No code yet; this is for review.

## The core problem: ids are device-local
`store` ids are monotonic PER INDEX. Device A's id 5 and device B's id 5 are different
records. So the existing `tomb` (a list of deleted *ids*) is meaningless across devices:
"B deleted id 7" cannot be applied to A. A cross-device merge must key on something stable
across devices, and must carry timestamps to resolve add-vs-delete conflicts.

## Identity = content; conflict = last-write-wins by ts
- **Identity.** A record's cross-device identity is its **content** `(keys, value)`. That
  is already the engine's implicit identity: `put` is idempotent by a content store-scan,
  so two indexes both holding `(keys,value)=X` hold the *same* logical record. No new UUID
  column (rejected: heavier, and it would change idempotent-put semantics).
- **Conflict.** Resolve with **last-write-wins by the store's `ts` column**: compare the
  latest ADD ts against the latest DELETE ts for that content. Delete-after-add removes it;
  re-add-after-delete keeps it. Deterministic for a single user across their own devices.

## What this needs on disk: timestamped, content-addressed tombstones
Today `tomb` is `id` per line: local-only and untimestamped, so neither portable nor
conflict-resolvable. Change it to carry a content hash + delete-timestamp:

    tomb (v2):   <ts>|<hash>          # hash = blake2b("keys|value"), truncated

- **Portable:** a deletion becomes a self-describing fact, not an index-local id, so it
  merges across devices AND survives compaction (which physically drops the deleted store
  line, today that loses the content needed to describe the deletion).
- **Timestamped:** enables last-write-wins.
- **Hash, not cleartext:** keeps `tomb` small and avoids writing a (possibly secret) value
  a second time in clear. Monocypher `crypto_blake2b` is already vendored.

Local `del(id)` becomes: resolve the record's content, append `<now>|<blake2b(content)>`.
Read-time suppression: build a {hash -> max delete-ts} map from `tomb`; skip a store record
whose content-hash is present with a delete-ts NEWER than the record's add-ts (so a re-add
after a delete reappears).

## Two tombstone types (both must merge)
There are **two** delete mechanisms today, both id-keyed and untimestamped:
- `tomb` — whole-record deletion (`del`, `del-key` cascade).
- `ktomb` — per-key removal (`del_key` strips one key from a record that otherwise stays).
Record-level (`tomb`) is the common case and covers v1. Key-level (`ktomb`) needs the same
content-addressing + ts treatment: a portable fact `<ts>|<record-hash>|<key>`. DECISION
for review: handle `ktomb` in v1, or ship record-level merge first and treat key-removals
as a documented follow-up? (Leaning: v1 = record-level; `ktomb` next, same pattern.)

## Merge algorithm (content-keyed, ts-resolved)
Given local index A and incoming index B:

    1. Stream B.store  -> for each record, key = blake2b(keys|value), event = (ADD, ts).
    2. Stream B.tomb   -> for each, event = (DEL, ts) under that hash.
    3. Per hash, take the MAX-ts event across A's own state and B's (A already knows its own).
    4. Apply to A:
         winner ADD, not already present -> put the record (idempotent).
         winner DEL                       -> ensure (ts,hash) in A.tomb; record stays suppressed.
    5. Rebuild idx/off (derived, as today).

Symmetric: run both directions (A pulls B, then B pulls A) and both converge to the same
live set. `--import` grows to understand DEL events (today it only adds).

## Wire / dump format (so deletions travel)
`--dump` today emits only live records and omits deletions. Add a delete line so a dump
(and the sync stream) carries both, with an explicit line type:

    A|<ts>|<keys>|<value>      # add  (today's dump line, now prefixed)
    D|<ts>|<hash>              # delete

`--import` applies `A` lines via `put` and `D` lines via the tomb/suppress path, both under
last-write-wins. Back-compat: an unprefixed legacy dump line = an `A` line with ts unknown
(treated as oldest, loses to any real-ts event).

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
1. `ktomb` (key-level) in v1, or follow-up? (leaning: follow-up)
2. Tomb hash width: 128-bit truncated vs full 256-bit.
3. `--dump` default: always emit the new prefixed format, or only under `--dump --sync`, to
   keep the human-readable/greppable dump unchanged?
4. Any existing consumer that parses `tomb` or `--dump` output? (grep before changing format.)
5. Store `keys` alongside the hash in `tomb` for a human-readable tombstone, or hash-only?
