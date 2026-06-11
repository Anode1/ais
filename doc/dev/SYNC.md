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
  key that literally starts with `-`). [+/- and propagation: planned; today
  `--del-key` removes locally only, no traveling marker]

## Delete -- soft, via Trash
- Delete a resource = unbind ALL its keys -> keyless -> Trash. Keyless is
  unreachable by recall, still visible to `--dump`/`--find`.
- Manual empty (`--compact`) physically purges keyless resources and spent `-key`
  markers. GC is a HUMAN decision (empty only when all devices have synced), so
  there is no distributed consensus and no resurrection. [planned; `tomb` exists
  for ids today]

## Sync -- bidirectional dump/import
- Merge = `--dump` one side, `--import` the other, both ways. The +/- patch is the
  interchange format.
- Adds union losslessly (a grow-only CRDT, no clock). Removes propagate as
  `-key`, latest-sign-wins. Deletes are untag-all -> Trash.
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
- Save emits the DIFF against the loaded state (only the +'s and -'s you changed),
  NOT the absolute keyset, so a concurrent add on another device is not wiped.
- `-key` is internal: the GUI toggles tag chips and emits it; users never type
  operators.
- Delete is an explicit, labeled, undoable action (-> Trash), not a silent
  consequence of removing the last tag.
