# AIS sync and identity -- decisions

Settled design for multi-device sync (home <-> phone, no server), so it is not
re-litigated. Some is implemented today, most is planned; implement to this
spec. See LAYOUT.md (format), BNF.txt (grammar).

## Identity -- a resource IS its value (the natural key)
- Dedup is by EXACT value; the numeric id is a device-local surrogate, never shared.
- URL/URI/inline text: same string = same resource (keys merge); different string
  = different resource. NO normalization (a query param can mean a different
  page). [implemented]
- Blob (`--doc`): identified by OCCURRENCE, not content. Each doc is its own
  resource, like a post -- duplicates are intentional and kept, NEVER
  content-deduped. Name = timestamp + a device/random tag, so two devices never
  alias two different posts and a replicated post keeps one name. [name tag: planned]
- Known seam (accepted): identity follows STORAGE, not intent -- inline values
  dedup (URL-like), blobs do not (post-like). So "intentional duplicate" is a
  blob-only concept. Fine for current usage.

## Keys -- a mergeable patch language
- `+key` binds (implicit default); `-key` unbinds (explicit, and it PROPAGATES).
- A put/import line is an idempotent keyset PATCH: apply the +'s and -'s.
- effective keys = bound minus unbound.
- Re-bind after unbind: LATEST-SIGN-WINS (loose timestamp; skew is immaterial at a
  multi-day sync cadence).
- Only a LEADING `-` is the sign; `-` elsewhere is literal (use `+key` to bind a
  key that literally starts with `-`). A detach propagates today as a `K|` line;
  the general `+/-` patch line remains planned.

## Delete -- a tombstone, kept for the life of the index
- Delete is delete. There is no trash and no restore: `--del ID` and
  `--del-under KEY` confirm first, then remove the record. The GUI's 5s undo
  window is the only reprieve, and nothing reaches the engine until it lapses.
- The id goes to `tomb` as `id|ts|hash`; recall, `--find` and `--dump` suppress
  it. `--compact` drops the record's BODY from the store and KEEPS the
  tombstone: it is the portable delete fact a peer needs, so collecting it would
  let any device that still holds the record push it back on the next sync.
- Tombstones are therefore never garbage-collected, by anyone, at any time. One
  ~42-byte line each is a rounding error against the store.
- Removing a tag is the reversible operation, and it is not a delete: `--untag
  KEY` (and `--update ID -KEY`) leaves every record live, keyless if that was its
  only key, and travels as its own fact (`ktomb`, the `K|` line).
- An encrypted value is shredded at delete time rather than at compaction, so a
  deleted secret is gone at once while a deleted plain record stays readable in
  `store` until `--compact`.

## Sync -- bidirectional export/import
- Merge = `--export` one side, `--import` the other, both ways; two-way
  convergence in one round is the `--sync` verb. The shipped interchange is the
  `A|`/`D|` merge stream (record-level, tombstone-union, timestamp
  last-writer-wins); the `+/-` key patch below remains planned, not the wire
  format today.
- Adds union losslessly (a grow-only CRDT, no clock). Removes propagate as
  tombstones: `D|` for a whole record, `K|` for a single key-detach, both
  content-addressed and resolved last-write-wins by ts.
- Blobs sync as files (rsync-style), device-tagged names, never content-deduped.
- Git or a file-sync app may TRANSPORT the bytes, but the MERGE must be `--import`
  (value-aware). Never trust git's textual merge of `store`, it id-collides.
- Idempotent and resumable: a partial/interrupted sync just re-runs to converge.

## Sharing -- give someone your index (bootstrap)
- An index is names, not secrets: it stores values (hosts, paths, URLs, commands),
  never credentials -- like a shared `.ssh/config` where each user keeps their own
  keys. So an index is safe to hand over or commit to a repo. [principle]
- Ready-to-use: ship the built index directory; the receiver queries it in place,
  `ais -f ./.ais KEY...`, no import -- a checked-in config. Filter first (dump only
  the records meant to be shared) so nothing private travels.
- Seed-and-own: hand over a portable dump and they make it their first version --
      ais --dump | sed 's/^[0-9]*|//'  >  shared   # donor: strip the local ids
      ais -f ./.ais --import           <  shared   # receiver: into a fresh index
  From there each side diverges and the Sync merge above reconciles both ways:
  value-as-identity dedups the shared records, each side's own records just add.
  [implemented]

## GUI / UX
- A resource = value (identity, shown read-only) + an editable keyset.
  - `--set` breaks this and DOES NOT SYNC. It edits the identity field in place,
    and the stream has no verb retiring the old value, so the peer keeps it and
    feeds it back: both values end up on both devices. Local-only until a
    value-retired fact exists on the wire (not the id-keyed tomb, which would
    suppress the record locally). The CLI warns on an index that has synced.
- Save emits the DIFF against the loaded state (only the +'s and -'s you changed),
  NOT the absolute keyset, so a concurrent add on another device is not wiped.
- `-key` is internal: the GUI toggles tag chips and emits it; users never type
  operators.
- Delete is an explicit, labeled action on the record, confirmed before it runs
  and undoable only inside the 5s window; it is never a silent consequence of
  removing the last tag. Removing the last tag leaves the record live and keyless.
