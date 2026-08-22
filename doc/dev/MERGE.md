# Seamless index merging — design note (IMPLEMENTED)

Goal: merge two AIS indexes (two devices, or two copies of one) into the union of records,
de-duplicated, with **deletions propagating**, deterministically. This is the engine
foundation under built-in sync (`doc/dev/SYNC_PROTOCOL.md`) and the manual merge case.

Status: built and tested in `c/` (`content_hash`, tomb v2 `id|ts|hash`, `feed_export`,
merge-aware `feed_import`, `ais_put_at`, `ais_merge_del`; round-trip test in `tests.c`).
As-built refinements vs the original draft: record identity is the **value** (not
keys+value); the content hash is **FNV-1a** of the value (not blake2b); **no migration**
(v1 `id`-only tomb lines coexist with v2). Key-level (`ktomb`) removals now merge too --
built and shipped: they export/import as `K|<ts>|<hash>|<key>` lines (feed.c `exp_kdead` /
`ktomb_each`, `ais_merge_detach`), so a detached tag propagates and stays removed after sync.
Multi-value (`ais_add`) grouping across a merge: SHIPPED, as the `M|` verb. Per-key attach
times (`katt`, the `T|` verb) and the true record time beside a raised export (`C|`):
SHIPPED, which is what makes a removed tag re-attachable at all.

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
- **Conflict.** Resolve with **last-write-wins by the record's EFFECTIVE time**: compare the
  latest ADD ts against the latest DELETE ts for that content. Delete-after-add removes it;
  re-add-after-delete keeps it. Deterministic for a single user across their own devices.
- **Against a `D|`, the record's time is `max(creation ts, last local edit)`**, not the
  store line's `ts` alone. An edit is a user action, and the same rule that keeps a re-add
  must keep an edit: otherwise re-tagging something another device deleted yesterday
  silently loses the most recent thing the user did. The edit time is local, in `mts` (see
  LAYOUT.md); a record that WINS is marked to EXPORT at that time (`sts`), which is how
  the decision reaches the other devices, and is the resurrect path's behaviour reached
  from the other side.
- **Only `D|` asks that question.** `K|` detaches and key attaches compare against the
  store's own `ts`. A detach is a targeted statement about one key; an unrelated edit says
  nothing about it, and letting the edit time answer a `K|` would mean adding any tag on
  one device re-attached a tag another device had deliberately removed, destroying the
  ktomb that proves the removal. A key attach therefore never reads the raised export
  timestamp either: the store's own `ts` travels beside it as `C|` and answers that
  question on its own (below).
- **A record that comes back comes back as it was just described**, whether that is a
  peer's `A|` line or a local re-save. The resurrect path replaces the record's key set
  with the arriving one instead of merging into the field the record carried when it was
  deleted. That field is a relic, and keeping it re-advertised keys other devices had
  deliberately detached -- at the record's new, later timestamp, which outranks their
  ktombs and re-attaches the tag across the whole mesh. Delete is delete, tags included.
  The rule is the same on both paths deliberately: keeping local relics while dropping
  every peer's live tags would be the worst of both, and it is the local relic that then
  travels as authoritative.
- **The raise goes one second past the tombstone, not up to the edit time,** and it
  applies to the EXPORT rather than to the store line (`sts`, see LAYOUT.md). It only has
  to outrank that one delete: every further second sweeps up unrelated key tombstones on
  other devices, because the exported timestamp decides key attaches too. Keeping it out
  of the line also keeps a `K|` detach's outcome from depending on the order peer bundles
  were read in.
- **The raise travels beside the record's true time, not instead of it.** The `A|`
  line still exports at the raised timestamp, because that is what the deleting peer
  must see to give up its tombstone. A `C|<true ts>|<hash>` line immediately before it
  carries the store line's own time, and the importer answers KEY attaches with that
  (`ais_put_at_k`'s `attach_ts`) while the record decision stays on the `A|`. Without
  it one timestamp answered both questions, so surviving a delete re-attached every
  tag any peer had removed:

      B removes a tag       10:00:00        (B and A never sync directly)
      C deletes the record  10:00:01
      A edits it            10:00:02        A exports at 10:00:02, tag included

  A's raised `A|` cleared the relay's ktomb before the relay could pass it on, so the
  removal never reached A and the two flipped on every round for as long as both kept
  publishing. With `C|` the relay keeps its ktomb; the detach reaches A one round
  after the record does (the relay has to resurrect the record before a `K|` can
  apply to it). Pinned in `c/tests.c`
  (`test_known_limit_restamp_outranks_an_unseen_detach` for this case,
  `test_later_detach_holds_in_every_sync_order` for the bound,
  `test_true_ts_verb_shields_key_tombstones` for the verb itself).
- **A delete newer than the edit still wins.** The edit clock raises a record's time, it
  does not pin it: delete-after-edit removes the record, as delete-after-add always did.
  Within one second the delete wins, since ties are sticky.

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

**A resurrect RESTAMPS the store line.** `put` is idempotent by value, so re-saving
something reuses the existing record -- and that record's `ts` is the add-ts this whole
scheme compares against a peer's tombstone. Reusing the line without restamping it made the
resurrection LOCAL ONLY: the record exported as an `A|` older than the delete, the peer kept
its tombstone and sent the `D|` back, and the re-save died on every device. Saving anything
the index had ever deleted was therefore impossible, permanently. `ais_put_at` now stamps the
line with the time it came back (the incoming ts on the merge path, `now` locally), which is
what "compare the latest ADD ts" above always meant.

RESOLUTION CAVEAT: `ts` is wall-clock UTC at ONE-SECOND resolution, and ties keep the delete.
A re-save inside the same second as the delete still loses. That is the same clock dependence
already noted for skew, and the same fix answers both -- a logical clock, or per-add unique
tags (an OR-Set), so ordering stops depending on wall time at all. Until then a re-save is
reliable at human timescales and unreliable at machine ones.

## The stream is extensible only because import REFUSES what it does not know

`--import` skips a line whose first field is a short uppercase token followed by a
timestamp and is not a verb it knows, and says so. Before that it fell through to
the plain `keys|value` reader, so a future verb became a record under a fabricated
key, silently, and `imported N` still reported success. That is why no new verb
could ever be added: every older peer would mangle it rather than skip it.

Two consequences, both binding:

- A new verb may only be WRITTEN a full release after the refusing build has
  reached every device. Older peers predate the refusal. The skip shipped in
  **v0.3.11**; `C|` and `T|` are written from **0.3.15**. The residual: a device
  last updated at 0.3.10 or earlier reads them as records under a fabricated
  `C`/`T` key.
- The verb must be a SHORT UPPERCASE TOKEN followed by a timestamp. The
  timestamp is what keeps a legitimate tag safe -- a hand-written `TODO|buy milk`
  has no date in field 2 and still imports as a record.

A bundle fed to `--import` is named and refused rather than parsed line by line;
it has a binary header and length-prefixed blobs, so reading it as a stream
invents records.

## The tombstone digest

`content_hash(value)`, FNV-1a. Identity has to be derivable from what two devices
can agree on with NOTHING shared -- no key, no pairing, no prior sync. The value is
the only such thing.

Salting it with the record's creation ts was tried and reverted. It looked free,
since both devices read the ts off the record's own line, but two devices that
independently save the same value stamp it at different times: they computed
different digests and a delete silently stopped crossing between them. The case it
still worked for -- a record that had already synced, so both carried the same ts --
is exactly the case that never needed help. Any future change to this digest must
survive that test: two devices, same value, different creation times, delete on one.

The cost of that constraint is real and is documented in `LAYOUT.md`: the digest is
a testable trace of a deleted value, kept for the life of the index and exported to
every peer. `--compact --forget-deleted` (`ais_compact_purge`) is the user's answer
-- it blanks the digest while keeping the id, so the record stays suppressed here
but the deletion stops travelling and stops being testable. The price, which the
caller must state: a peer that has not synced since can push those records back.

Migrating old tombstones is impossible and is not attempted: for any that survived
a compaction the value is gone from the store, so their digest can never be
recomputed. A privacy fix therefore cannot work by changing the function -- only by
forgetting, or by an identity not derived from content at all, which needs a wire
generation change.

## A record's extra links travel as M|

A record can hold several values, each its own store line, and the wire had no way
to say they belong together: an export emitted every line as its own `A|`, so the
importer created a SEPARATE RECORD per value and every restore silently split one
3-link record into three. Through `--dump`, `--export` and the sync bundle alike.

    A|<ts>|<keys>|<first value>
    M|<ts>|<hash of the first value>|<another value>

The first value carries the record; the rest attach to it by that value's hash
(`ais_merge_addval`). Idempotent, because sync repeats by design -- a folder sync
re-imports the same bundle every pass, and `ais_add` appends unconditionally, so
without a check the same link stacked up on every round.

An older peer skips `M|` (see the extensibility rule above) and gets the record
with one link: lossy, but no longer WRONG, and strictly better than the three
separate records it used to invent.

Document bodies travel the same stream as `B|<path>|<size>` plus raw bytes. They
always did for sync bundles, which is why a folder sync carried documents while
`--export` -- the documented backup route -- dropped them, restoring a record whose
body simply did not exist.

## An edited value travels as a retirement

`ais_set_value` replaces one value in place, keeping the id, ts and keys, and the
stream needs no verb for it: the old value is retired as a `D|<now>|<hash>`, the
fact every delete already travels as, and the new value goes out as the record's
ordinary `A|`. A peer drops its copy of the old value and creates the new one, so
one round leaves the new text alone on both sides. Three details make it hold:

- **The tombstone is written under id 0.** `tomb` suppresses by id, and the
  record's own id would suppress the edited line. Id 0 names no record, so the
  line stays live here while the fact travels; `exp_dead` ignores the id.
- **Editing back to a value this index once retired** removes that tombstone,
  as a re-save does, and raises the export (`sts`) to the edit time, because the
  peer holds its own tombstone for that value and the `A|` has to outrank it.
- **`ktomb`/`katt` name the record by its first value's hash.** When that is the
  value replaced, the entries are re-keyed (`ktomb_rehash`, `katt_rehash`), or a
  detach made before the edit would travel under a name no peer holds.

What a peer cannot tell is that the two values were one record. A delete it
makes of the OLD value after the edit names a hash this index no longer holds
and is skipped, so the edit survives it, exactly as a fresh save of the new text
would. Pinned by `test_set_reaches_the_peer`.

## A raised export carries its true time as C|

    C|<the store line's own ts>|<hash of the record's FIRST value>
    A|<the raised ts>|<keys>|<first value>

Emitted only when `sts` actually raised the exported timestamp, so a normal index
writes none. Keyed to the FIRST value because a multi-value record's later values go
out as `M|`; a `C|` keyed to one of those would name a line the importer never puts.

The importer holds ONE pending `C|` and spends it on the next `A|`, matched or not:
the exporter emits the pair adjacently, so a slot that outlives its `A|` could only
mis-answer some later record. A matching hash makes the `C|` time the `attach_ts`
argument of `ais_put_at_k` -- the value `attach_wins` compares a key attach against --
while the `A|` timestamp goes on deciding the record against tombstones, exactly as
before.

An older peer skips the line (the unknown-verb rule below) and converges precisely as
it does today: the raised `A|` answers both questions there, which is the outcome that
shipped. Pinned by `test_old_peer_ignores_the_new_verbs`.

## A key put back on travels as T|, against katt

A detach used to win for ever. `ais_merge_detach` judged an incoming `K|` against the
record's timestamp, and a detach is by construction stamped at or after the record was
created -- so it always won. Once a key had been removed anywhere in the mesh, no
device could put it back: the re-attach showed locally, then the peer's `K|` came round
and undid it on the very device that had made it.

    B detaches "work", syncs      -> gone on both        (correct)
    A re-attaches "work" locally  -> A shows it
    after syncing                 -> gone on A too, ktombs back

The missing fact is WHEN the key went on, which the `A|` line cannot carry: it has one
timestamp for the record and its whole key set. `katt` records it -- ktomb's mirror
image, the same `id|ts|hash|key` line, the same accessors, described in LAYOUT.md --
and it travels as `T|`, `K|`'s mirror on the wire. Three rules:

- **A local attach to a record that ALREADY EXISTED notes the time**, and clears any
  ktomb for that key as it always did. A brand-new record notes nothing: its keys went
  on at its own timestamp, which the `A|` line already carries.
- **An incoming `K|` applies only if it is at least as new as `max(the record's own
  time, katt)`.** This is the rule that needs `C|`: against a RAISED record timestamp
  it would let an unrelated survival outrank a genuine later removal.
- **An incoming `T|` applies only if it is strictly newer than any local ktomb for
  that key** -- attach_wins' rule, and `K|`'s exactly mirrored. It is applied through
  the same `ais_post_keys` every other attach goes through, so the authoritative keys
  field is written and not just the posting.

`T|` lines are emitted AFTER the `A|` lines, from `katt` directly: the record has to
exist on the far side before a key can be attached to it. An index that has never
re-tagged an existing record emits none.

They arrive in one contiguous run (one `katt` pass produced them), and the importer
buffers that run and resolves it through `ais_merge_attach_many` -- ONE store pass for
up to `AIS_ATT_BATCH` facts, the same treatment `D|` gets from `ais_merge_del_many` and
for the same reason. A scan per fact made an import cost O(attaches x records), and
unlike `K|` -- which exists only for the rare deliberate detach -- a `T|` is emitted for
every key attached after its record was created, which on an index whose tags grew with
it is most of them. Applying a fact rewrites keys fields and postings but never a
VALUE, so the hash->id map one pass builds stays valid across the batch. The batch is
smaller than `AIS_MERGE_BATCH` because a fact carries a whole key and the importer
buffers them on its stack.

The other half of that cost was `katt_set`, which replaces an entry and so rewrites the
whole file. On a first import every arriving fact is new, so there is nothing to
replace; `att_apply` reuses the lookup it already did and appends (`katt_add`) instead.
Local attaches take the same route. What remains is one `katt` scan per fact, the
linear-file cost `ktomb` shares; making it constant needs an accelerator beside `katt`,
which is not built.

Compaction keeps a `katt` entry only while it is still true -- the record live, the key
still attached -- because a stale one would go on advertising an attach that no longer
exists. Detaching a key drops its entry, and deleting a record drops all of them, on
the same "delete is delete" rule that clears `mts` and `sts`.

## Two tombstone types (both must merge)
There are **two** delete mechanisms today, both id-keyed and untimestamped:
- `tomb` — whole-record deletion (`del`, and the `del-under`/`del-key` cascade).
  `del-under` RE-STAMPS a record that was already tombstoned, so "delete everything
  under this key" holds as of now and a peer add dated between the two deletes stays
  suppressed. It does not COUNT that record: the count is live records only, so the
  confirmation prompt and the result line agree.
- `ktomb` — per-key removal: `update ID -KEY` strips one key from a record that otherwise
  stays, and `untag KEY` does the same across every record filed under KEY --
  including records already tombstoned, whose detach must still be recorded or a
  later resurrect brings the key back at the next compaction.
  NOT `del_key`, which despite the name deletes whole records and writes `tomb`.
Record-level (`tomb`) is the common case and covers v1. Key-level (`ktomb`) gets the same
content-addressing + ts treatment: a portable fact `<ts>|<record-hash>|<key>`, emitted on the
wire as `K|<ts>|<hash>|<key>` and merged via `ais_merge_detach`. SHIPPED -- both tombstone
types now merge, so a per-key removal propagates and does not reappear after sync. `ktomb`
alone was one-way, though: see `katt` and `T|` above for the fact that lets a key come back.

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
    C|<true ts>|<hash>         # the true time of the A| that follows (raised exports only)
    D|<ts>|<hash>              # delete (a content-addressed tombstone)
    K|<ts>|<hash>|<key>        # key-detach (a content-addressed per-key removal)
    T|<ts>|<hash>|<key>        # key-attach (when a key went ON, K|'s mirror)

The `K|` line carries a per-key detach: `<hash>` is the record's content hash and `<key>` is
the single (encoded) key to strip, removed at `<ts>`. Like `D|` it is a portable, content-addressed
fact (the `ktomb` tombstone) that MUST propagate so a tag a user removed on one device does not
reappear the next time that device syncs -- the removal wins by last-write-wins the same way a
whole-record delete does, but scoped to one key, leaving the record and its other keys intact.

`--import` applies `A` lines via `put`, `D` lines via the tomb/suppress path, and `K` lines via
`ais_merge_detach` (the `ktomb`/key-suppress path), all under last-write-wins. A plain (unprefixed)
line fed to `--import` from a file/stdin is treated as an `A` line with ts unknown (oldest), so a
hand-edited or legacy dump still imports as adds.

## Deletes resolve a batch at a time

A `D|` line names a value HASH, so finding the record it names means scanning the store.
One scan per line cost O(deletes x records): on a 20k-record index, 50 deletes took
0.06s, 200 took 0.24s, 800 took 1.04s -- minutes on a phone syncing with a peer that
has deleted a lot. `--import` buffers a run of `D|` lines (`AIS_MERGE_BATCH` = 256
facts, a fixed stack buffer, flushed when full and at end of stream) and resolves the
whole run in ONE pass, `ais_merge_del_many`. The same 800 deletes now cost 0.04s.

Two rules keep the outcome identical to resolving each line on its own:

- **The batch spans a RUN of `D|` lines only.** Every other verb either writes a record
  or reads one and must see the tombstones the pending deletes write: an `A|` can
  resurrect the very record a `D|` names or raise its edit clock, and a `K|` asks
  whether the record is still live. Anything that is not a `D|` flushes the buffer
  first. In practice the run is all of them, since an export emits every add before the
  first delete.
- **The seeking is shared; the applying stays in STREAM order.** Nothing an apply writes
  (`tomb`, `mts`, `sts`) is read by a seek, so moving the seeks cannot change an
  outcome -- but two facts can name two values of ONE record, and then the order decides
  which value's hash the tombstone carries onward to the peers. Applying in store order
  silently swapped it.

`K|` has the same per-line scan and could share the mechanism: `ais_merge_detach` writes
`ktomb` and postings, never the store, so the same seek-then-apply split holds, and the
wire already emits the detaches in their own run after the deletes. `M|` cannot:
`ais_merge_addval` appends to the store, and a line added part-way through a pass changes
what the rest of that pass finds. Nor can `T|`, for the same reason: an attach has to
reach the authoritative keys field, so `ais_merge_attach` rewrites the record's lines.

## Migration
None. v1 (`id`) and v2 (`ts|hash`) tombstones coexist, so old indexes keep working without a
rewrite (see the "no migration" note in the header). The earlier plan to bump `version` and
rewrite v1 tombstones to `0|hash` on open was not taken.

## Edge cases / assumptions
- **Clock skew.** Last-write-wins assumes roughly-synced clocks (NTP-normal). A badly skewed
  device could mis-order a delete vs a re-add. Acceptable for single-user; Lamport/vector
  clocks are the heavier fix if ever needed (out of scope).
- **Content collision.** Two different notes with identical `(keys,value)` are one logical
  record by design, same as today's idempotent put. Acceptable.
- **Edit = del + add** at the content level; nothing special.
- **Hash width.** FNV-1a 64-bit, as built; not a security boundary (content is
  already content-addressed).

## Open questions
1. RESOLVED: `ktomb` (key-level) merge is SHIPPED -- key-detaches travel as `K|` lines
   (`ais_merge_detach`), alongside record-level `tomb`, and key-attaches as `T|`
   (`katt`, `ais_merge_attach`), so a removal is reversible.
2. RESOLVED: the tomb hash is FNV-1a 64-bit (see "The tombstone digest").
3. RESOLVED: plain `--dump` stays unchanged (readable); the prefixed `A|`/`D|` lines are the
   export-wire format only (see "Export-wire format" above).
4. Any existing consumer that parses `tomb` or `--dump` output? (grep before changing format.)
5. Store `keys` alongside the hash in `tomb` for a human-readable tombstone, or hash-only?
