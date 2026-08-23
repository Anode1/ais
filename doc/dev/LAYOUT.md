# AIS -- On-disk layout and module map

The contract the implementation honours. See `doc/dev/STYLE.md` for the coding
ideology (C99/Robbins, stack/streaming, append-only, modular). Nothing in AIS
is hashed: every file is plain text, readable, greppable, repairable by hand.

## An INDEX is a directory

    INDEX/
      store           append-only records:  id|ts|keys|value  (one per line)
      next_id         a single line: the next id to assign
      version         on-disk format version (>= 2 carries the ts column; see below)
      idx/<p>/<key>   posting list for a key: ids, one per line, ascending
      off             id->offset accelerator: line k = byte offset of id k
      multi           ids carrying >1 value line (from add)
      tomb            tombstones: deleted ids, one per line
      mts             when each record was last edited HERE: one fixed-width slot per id
      sts             the time a record must EXPORT at after surviving a peer's delete
      ktomb           key-detaches: id|ts|hash|key, one line per removal
      katt            key-attaches: the same line, for a key put on a record that
                      already existed -- ktomb's mirror image
      edits           in-place value edits: ts|hash|value, one per edit, kept like
                      tomb and exported as E| (MERGE.md)
      foldsync        DEVICE-LOCAL: shared folders this device has synced with, one
                      absolute path per line (never synced, never exported)
      syncfolder      DEVICE-LOCAL: the folder the GUI syncs with (written by the app)
      blobs/<ts>~<tag>.txt   documents saved by `doc` (real data; not rebuildable)
      lock            writers' advisory flock (per op; reads lock-free)

### store -- the source of truth (append-only)
`id|ts|keys|value`. `id` is a positive integer, assigned monotonically. `ts` is
the save time, written on every put as UTC to the second, `YYYY-MM-DDThh:mm:ssZ`
(one canonical instant across devices). `keys` is one or more space-separated
encoded keys. `value` is the literal resource: a URL, URI, absolute path, or a
path relative to INDEX (relative keeps the whole INDEX portable). Because ids are
monotonic, the store is physically in id order.

### mts, the edit clock

One fixed-width slot per id, exactly like `off`: 20 characters of timestamp plus a
newline at offset `(id-1)*21`, blank means never edited. O(1) in and out, no
parsing, nothing allocated, and the file is bounded by the highest id rather than
by the number of edits ever made. A deleted record's slot is blanked (delete is
delete, including the note of when it was last touched), and compaction blanks any
that were missed.

It exists because merging compared a peer's delete against the record's *creation*
time. A record created Monday, deleted on the phone Tuesday and re-tagged on the
laptop Wednesday looked like a Monday record against a Tuesday delete, so
Wednesday's edit was destroyed. That contradicts the rule the resurrect path
already sets: a later user action beats an earlier delete, and an edit is a later
user action. Every local way of changing a record stamps it: `--update`, `--set`,
`--add`, and re-saving a value the index already holds, which is how a tag gets
attached and what every GUI save path calls. An import does not: an arriving
record carries its own time.

That applies to an arriving `M|` link too, though it lands through the same
`--add` the local path uses (`add_link`'s LOCAL flag is what separates them).
Stamping it made a record's fate depend on WHEN its bundle was read: given the
same `M|` and `D|` facts in two peer bundles, a folder sync resolved the delete
one way or the other by `readdir` order -- the very non-determinism `C|` and
`sts` exist to remove.

It is never exported, and that is the whole point of keeping it separate. The
exported `A|` line carries ONE timestamp for the record and its entire key set,
and the import side hands that same timestamp to `attach_wins`. Raising it would
not merely outrank record deletes, it would outrank KEY deletes: a device that
removed a tag would see it come back the moment any other device touched the
record for an unrelated reason, and the `ktomb` proving the removal would be
erased. For the same reason `mts_effective` answers only the record-delete
question (`ais_merge_del`), not `K|` detaches, which are targeted statements about
one key that an unrelated edit says nothing about.

What travels instead is `sts`, a second file of the same shape: the time a record
must export at, once it has survived a peer's delete. Without it the decision would
be local only -- the deleting device would keep its tombstone and send it back every
round, and the record would flap in and out on every device but this one. The raise
does not answer key questions on the far side either: the export sends the line's
true time with it, as `C|`, and only key attaches read that (MERGE.md).

Three rules keep that raise from doing damage:

- **One second past the tombstone, not up to the edit time.** Beating that one
  tombstone is the whole job, and the exported timestamp also decides key attaches,
  so every further second sweeps up unrelated key tombstones on other devices.
- **Out of the store line.** Written into the line, a `K|` detach arriving later in
  the same import pass compared against the raised value and lost, while one
  arriving earlier compared against the creation time and won -- and which you got
  was the order `readdir` returned the peer bundles in. It also rewrote the whole
  store and dropped the `off` accelerator on every sync round.
- **A record that comes back adopts the key set it was just described with**, not
  the one it was deleted with, or the stale field re-advertises tags other devices
  removed (see MERGE.md).

Both files are cleared for an id the moment its record dies, by either delete path,
and compaction blanks any that were missed.

The slot is validated before anything acts on it. `off` can afford to be trusted:
it is a pure accelerator whose worst failure is a fallback scan. This value can be
sent as a record's exported timestamp, and the resurrect path writes an incoming one
into a store line, where a stray delimiter or newline would split the record and lose
its value, so a slot that is not exactly a canonical
timestamp reads as "never edited".

Restamping on *every* edit instead of keeping a sidecar would be five lines, and
fails for the same reason the export does: it feeds the key-attach comparison. It
would not have disturbed "Recent", which pages by id (`ais_timeline` walks ids
downward), only the displayed date.

One-second timestamps mean an edit and a delete inside the same second still
resolve to the delete, since ties are sticky. That is the clock's limit, not this
file's.

**The keys field is authoritative, and every key-attach must reach it.** The store
is the source of truth and `idx/` is a rebuildable accelerator, so a key that lives
only in a posting list does not exist as far as the store is concerned: compaction
rebuilds `idx/` from the store lines and silently drops it, `--dump` prints the
stale field, and `--export`/`--import` never carry it. Attaching a key therefore
rewrites the record's lines in place (all of them: every line of a multi-line
record repeats the same keys field), the same rewrite compaction and
`ais_set_value` perform. This is the one exception to append-only besides
compaction and the in-place value edit; it is cheap because it is rare (a re-put
with new keys, or `--update`). The keys field stays the authority on WHICH keys a
record carries; `katt` (below) records only when a later one went on, which the
field cannot say. Attach is folded first (`|` and
control bytes to `_`, as `keys_attach_only` does for a put): a stored key shares
the line's `|` delimiter, so a raw `a|b` would shift the value into the wrong
field, and a raw newline would end the line and drop the value.

Detach is NOT symmetric. It removes the posting and records a `ktomb` entry, and
the key stays in the keys field until compaction strips it (`compact_visible_keys`).
So the keys field is the authoritative record of what has been ATTACHED, not the
live key set: a detached-but-not-yet-compacted key is still in it, and `ktomb` is
what makes the removal true and lets it propagate to peers (I1).

The keys field propagates the attach itself, in the `A|` line -- but not WHEN. The
line carries one timestamp, the record's, so a key put on later looked exactly like
one the record was born with, and lost to any detach in between: once a key had been
removed anywhere in the mesh, no device could put it back. `katt` is the answer and
the exact mirror of `ktomb`, same `id|ts|hash|key` line and the same accessors
(`kfile_*` in compact.c): one entry per pair, written only for an attach to a record
that ALREADY existed, dropped when the key is detached or the record deleted, and
swept by compaction. It travels as `T|` (MERGE.md) and is what an incoming `K|` has
to beat.

**A record may have NO keys.** `untag KEY` leaves one behind whenever `KEY` was a
record's only key, and the store represents that as an empty keys field. Such a
record is still live: it keeps its id and value and answers `--find`, `--timeline`
and `--dump`; it is simply not filed under anything. Anything that reads a record
line must carry it through, and the dump grammar says so outright: a keyless
record is `-v VALUE`. No voucher, no typo heuristic. Keys cannot be omitted by
accident the way a field before a `|` could be left empty, because the marker
states the intent. (This used to need an id on the line to tell a deliberate
keyless record from a slip; the ambiguity went with the format. See
[FORMAT_V2.md](FORMAT_V2.md).)

**A VALUE NAMES ONE RECORD.** This is the engine's identity rule and every write
path enforces it: `ais_put_at_k` resolves an existing record by `store_find_value`
and reuses its id, `ais_merge_addval` resolves by `content_hash`, `ais_set_value`
refuses a value another record holds, and `ais_add` does the same (returning -2 so
the CLI can say why). It has to hold, because everything above it assumes it:
tombstones are hash-stamped, the merge stream is hash-keyed, and two records
sharing a value make a peer collapse them, after which a delete of either takes
both. `ais_add` was the one path missing the guard until 2026-08; the invariant is
pinned end to end in tests/cli.sh under "identity:".

**An interrupted compaction is rolled back at the next open.** `compact_locked`
stages the old posting tree in `idx.bak` and restores it on a graceful error, but
a SIGKILL, an OOM kill or a power cut never reaches that path: what it leaves is a
half-built `idx/` live and the good tree orphaned. `get()` reads `idx/` with NO
store fallback, so every keyed lookup then returns a SUBSET, exit 0, no warning,
until somebody happens to compact again -- measured at 180 of 1492 records.
`compact_recover()` runs from `ais_open` and puts the staged tree back. Restoring
it is safe in both windows because postings are keyed by id and compaction
preserves ids, so the pre-compaction tree is a superset and reads filter
tombstones anyway. A superset that reads correctly beats a subset that lies.

**Tag counts are filtered at READ time, not pruned at delete time.** A posting
keeps a deleted record's id until the next compaction, so counting lines reported
tags that answer nothing -- the tag list said "notes 120" while querying `notes`
returned zero, and in the GUIs, where the tag list IS how you browse, tapping it
gave 0 results. `ais_tags`/`ais_keys` skip tombstoned ids and omit a key whose
every record is dead. Gated on `tomb_active()`, a single stat, so an index with
nothing deleted pays nothing. Pruning at delete time instead would turn an O(1)
append into a walk of every key the record carries, on the phone's hot path.

**A long-lived process must re-read `next_id`.** It is cached at open, and
`ais_timeline` took its ceiling from it, so a server or a phone app that stayed up
never showed anything another writer added -- including records arriving by sync.
`get`/`tags` read the postings each call and did see them, so only "Recent" lied.
The refresh has to happen BEFORE `off_consistent()`, which compares the `off` file
against `next_id` too: a stale counter also mis-declares the accelerator
inconsistent and diverts to the scan path.

**Format versions.** v1 was `id|keys|value` (no `ts`); v2 added a local `ts`;
v3 makes `ts` UTC with a trailing `Z`. `INDEX/version` records the format, and
`store_open` stamps the current version into an older index in place the first
time this build touches it (a too-old `ais` then refuses it, rather than misread
a `ts` as keys; a *newer* format is refused too). The parser reads every shape:
it treats field 2 as `ts` only if it looks like a date (a full `YYYY-MM-DD`,
optionally `...Thh:mm[:ss]`, optionally `Z`); anything else -- including a *year
used as a key* like `2026` -- stays the keys field. So a missing, empty, or
malformed date never loses a record: the line reads as a dateless v1 record
(id, keys, value intact) and the timeline surfaces it FIRST. Timestamps are NOT
identity: `dump` emits `KEY... -v VALUE` and carries neither the id nor the ts,
because `import` reassigns both. The date is read only for the
timeline/tags views -- the recall path never parses it beyond the field-split.
(Pre-v2 archives carry no `ts`; the filesystem mtime of `store`/`idx` files is
the only date such records have, and it is reset by compaction or a copy that
does not preserve times.)

A record may hold several values (multi-link): `add` appends another
`id|keys|value` line with the same id. `ais_record(id)` resolves a single-value
record by one seek via `off` (below); a multi-value id falls back to a store
scan that collects every line bearing that id.

### next_id -- monotonic id counter
Holds the next id as text. On put: read, use, write back +1. If missing
(hand-edited store, first run), recovered by one streaming pass over `store`
taking max(id)+1 -- bounded memory, one `long`.

### idx/<p>/<key> -- posting lists, sorted by construction
`<key>` is the encoded key (lowercased; space, control, `|`, `/` and `\` -> `_`,
so it is one store field and one safe path component). The file is that
one key's list of record ids, one per line, ascending. `<p>` is a short
NAVIGABLE prefix of the key (first one or two encoded CHARACTERS, never a
partial UTF-8 sequence): `idx/a/apple`, `idx/日/日本語`. Two BYTES was the rule
until a three-byte character showed what that means: the directory name was half
a character, which Linux stores happily and APFS refuses, so on macOS and iOS the
posting was never written and the key recalled nothing.
The prefix keeps the index human-walkable -- `ls idx/a/` shows keys beginning
with `a`. No hashing: keys are human words, kept as themselves (git shards by a
hash prefix because its keys are hashes; ours are words). (If a prefix bucket
ever grows large, split it adaptively by the next character.) Each key being its
own small file is also why sync is cheap: `rsync` transfers only the keys that
changed and its log names them, where a single binary blob resends on any change.
Per-key sharding is chosen for corruption-resilience too: a smashed shard costs
only its keys while the rest of the index still answers, the same reason the
store is plain text (damage stays local, never global). It is the
log-structured-merge / git pattern: sharded loose files on write, packed on
compaction.

Sorted by construction (with one ordered-insert exception): for a brand-new
value `put` appends the new largest id to each key file, ascending by pure
append. The exception is re-filing an *existing* (older) value under a NEW key:
its id must enter that key's file in order, a bounded in-place ordered insert
(append on the fast path; merge-into-temp + atomic rename only when the id lands
out of order). Either way every posting file is kept ascending and
duplicate-free, so the read path never sorts -- `get` is a pure merge.

### off, multi -- the record fast path (pure accelerators)
`get` yields ids, then resolves each to its value(s). Scanning `store` per id is
O(matches x store) -- the one real bottleneck at scale. `off` fixes it: a
fixed-width text file, line k = the byte offset (stored +1, so 0 = absent) of
id k's first `store` line. Every seek user (`ais_record`, the timeline, the
delete stamp) re-checks the line's id at the offset and falls back to a scan on
a mismatch, so a stale offset never returns wrong data -- and never silently
drops a record either: the timeline once trusted a slot that was one byte off
and lost a record that recall still found. `off` is maintained in lockstep by
`put`, from the offset `store_append` itself reports (the store's size
beforehand is wrong by one whenever the tail needed closing, below), and rebuilt
by `compact` (with `0` sentinels for the gaps that dropped ids leave).

`multi` lists ids carrying more than one value line (from `add`, whose
continuations are scattered); the fast path skips these and scans, so multi-value
records are always read in full. Both files rebuild from `store` -- delete them,
`compact`, and nothing is lost.

**Both are staged and renamed AFTER the store's own rename, never truncated in
place.** They are accelerators, but a WRONG one is not merely slow: with `multi`
empty against a store that still holds the continuation lines, the export reads
`multi_contains()==0` for a genuinely multi-value record and emits each of its
values as its own `A|`, so one record arrives on every peer as several. Rebuilding
them before the store was committed meant a compaction killed in between left
exactly that state, and `compact_recover` restores only `idx/`. Renaming after the
store means they can only ever describe a store that is already in place; the one
surviving failure -- the OLD pair against the NEW store -- is safe, because `off`
is size-checked (`off_consistent`) and id-verified on use and a stale `multi` only
forces the slow path.

### Appending to the store: never onto an unterminated line
A record can be `AIS_LINE_MAX` (64 KB) against stdio's 4 KB buffer, so one append
is several `write()` calls, and a power cut or `ENOSPC` between them leaves a line
with no newline. Append mode would then write the NEXT record straight onto its
end and the two fuse: `store_parse` reads the leading id and takes everything
after it -- including the whole following record -- as one value. The second
record becomes unreachable and the next compaction writes the fusion back
verbatim, destroying it permanently. Two records lost from one torn write.

`store_append` therefore checks the final byte and closes the line first. It ADDS
a newline rather than truncating the tail: a store is meant to be hand-editable,
an editor that leaves no final newline is ordinary, and that last line is then a
complete record truncation would delete. A genuinely torn line survives as its own
short line -- damaged, but damaged alone, which is the promise in STYLE.md.

There is no `fsync` on this path: it would cost a disk flush on every save to
narrow a window this already makes survivable, and compaction's rename commit
takes the same view.

### tomb -- tombstones
`del(id)` appends the id to `tomb`. `get`/`dump` merge it out (suppress ids
present in `tomb`). Compaction drops the deleted record's BODY from the store but
KEEPS the tombstone (`tomb_keep_hashed`): the tombstone is the portable delete
fact a peer needs, so collecting it would let any device that still holds the
record push it back on the next sync. Tombstones are retained for the life of the
index -- one ~42-byte line each, which is a rounding error against the store.

The line carries `content_hash()` of the DELETED VALUE, and that is FNV-1a, not a
cryptographic hash. A guessable value (a phone number, a short URL) is recoverable
from it in seconds, and the tombstone is exported to every peer and kept forever,
so a delete leaves a permanent, testable trace of what was deleted.

SALTING IT WAS TRIED AND REVERTED, and the reason is worth keeping. Salting with
the record's creation ts looked free -- both devices read the ts off the record's
own line, so nothing had to be shared. But two devices that INDEPENDENTLY save the
same value stamp it at different times, so they computed different digests and the
delete silently stopped crossing between them. That is the whole point of
content-addressing: identity must be derivable from what both sides can agree on
with nothing shared, and a per-device timestamp is not that. It is only equal when
the record itself was synced, which is exactly the case that did not need help.
Anything that changes this digest must survive the same test: two devices, same
value, different creation times, delete on one. This is the
same asymmetry noted above for blobs, in the other direction: an ENCRYPTED value
hashes its ciphertext, so its tombstone reveals nothing and its blob is shredded
at delete time. Do not tell a user a plain delete leaves nothing behind.

`--compact --forget-deleted` (`ais_compact_purge`) is the user's answer: it blanks
the hash while keeping the id, so the record stays suppressed here but the delete
stops travelling and stops being testable. The cost is real and the CLI states it
before asking -- a peer that has not synced since can push those records back,
because this index can no longer tell it they were deleted.

Migrating old tombstones is impossible and does not need to be attempted: for any
that survived a compaction the value is gone from the store, so their digest can
never be recomputed. That is why the matcher accepts BOTH forms rather than
converting anything: old tombstones keep working, and `--forget-deleted` retires
them when the user wants. Salting the digest with the record's creation time was
tried and REVERTED, because it broke the one thing that must never break: two
devices that saved the same value at different moments computed different
digests, so a delete stopped crossing between them. Identity is the value and
nothing else (`c/ais.c`, mdel_seek), because it has to come from what both sides
agree on with nothing shared.

**A delete takes the file the INDEX made, and never the file it merely points
at.** That line is the whole rule, and it is about ownership, not about how a
value looks: `blobs/<ts>~<tag>.txt` and `blobs/<ts>~<tag>.aisc` are documents this index
wrote, so they go when their record goes (`ais_doc_discard`, doc.h); a path
anywhere else on disk, a URL, or inline text names something that was the
user's before ais saw it, and nothing happens to it, ever. The product is an
index of your things where they are, so destroying one would be the worst bug
this program could have.

Three moments dispose of a payload, and every front end needs all three:

- **A local delete**: the front end calls `ais_doc_discard` before tombstoning
  (`--del`/`--del-under` in main.c, serve.c, embed.c), and after a `--set`
  replaces a value the store no longer points at.
- **A delete arriving from a PEER**: `ais_merge_del_many` hands each retired
  value to the disposer registered with `ais_on_discard`. Without it a document
  deleted on the laptop stayed on the phone forever -- and since an export
  streams the whole of `blobs/`, the phone handed it straight back to the laptop
  on the next round. The engine itself stays oblivious: it reports a value whose
  last reference it dropped and lets the front end decide, because only the
  front end knows what a path means.
- **Compaction**: dropping a tombstoned store line is the last moment anything
  knows the file was referenced, so `compact_line` discards it there. This is the
  sweep for deletes made before any of the above existed.

Encrypted and plain differ only in HOW: ciphertext is zero-filled before it is
unlinked (`secret_shred_blob`), a plain document is simply removed. Neither
overwrite is a security boundary on flash or CoW storage; a document that must be
unrecoverable should have been saved with `-e`.

The consequence to know: a deleted record's LINE is recoverable until `compact`
(truncate `tomb`), but its blob is gone at delete time. Do not tell users a
delete is undoable.

### Idempotent put -- by store scan, no index, no hash
`put(keys, value)`: find whether `value` is already stored by streaming `store`
and comparing the value field (the store IS the value->id map). If found, reuse
that id and add any new keys to its posting lists. If new: id = next_id++,
append the store line, append id to each key's posting list. Identical re-puts
change nothing. O(n) per put, fine at personal scale. An import does not pay it
per line: feed.c spools a run of adds and resolves their values in one pass,
then puts each with the answer (`ais_put_at_k_resolved`).

### get -- streaming k-way merge
Open one stream per query key (<= AIS_KEYS_MAX), each at its current head id
(one `long` per key). AND: emit an id when all heads equal it, then advance all;
otherwise advance the stream(s) at the minimum. OR: emit the minimum head,
advance every stream at it (dedup). Suppress tombstoned ids. Memory is O(nkeys).

### compact -- streaming rewrite
Stream `store` dropping tombstoned ids into `store.new`; rebuild `idx/`, `off`
(first-line offset per id, `0` sentinels for gaps) and `multi` in the same pass;
rename atomically; clear `tomb`; recompute `next_id`. Bounded buffers throughout.

### import -- the editable batch format (inverse of dump)
`ais --import` reads `keys|value` lines from stdin and `put`s each -- the inverse
of `ais --dump` (drop the leading `id|`), so an index round-trips:
`ais --dump | sed 's/^[0-9]*|//' | ais --import`. Blank lines and `#`-comments are
skipped (the file stays hand-editable); idempotent re-import changes nothing.
Lines that share keys recall together.

### import-interactively -- pick records as they go by
`ais --import-interactively` is `--import` with a per-record `[y/N]` gate: each
`keys|value` line is shown and only taken on `y` (`N`, the default and a bare
Enter, skips). It reads the same `keys|value` lines as `--import` from stdin and
takes the answers from `/dev/tty` (or `$AIS_TTY`), so the two streams stay
separate exactly as `-i` keeps values and keys apart. To review another index,
strip the `id|` from its dump just as `--import` expects:
`ais -f OTHER --dump | sed 's/^[0-9]*|//' | ais --import-interactively`; or sip a
shared `keys|value` file directly. For adopting bits of someone else's shared
index without polluting your own; merging your OWN indexes across devices is the
bulk `--dump | --import` instead.

### doc, blobs/ -- large or multi-line values
A value is one line, so multi-line/large text can't be inline. `ais --doc KEYS`
reads a document from stdin, writes it to `blobs/<timestamp>~<8 hex>.txt` (local
time, so `ls blobs/` reads chronologically; the random tag is what stops two
DEVICES minting one name for two documents, and a same-second doc on the same
device still gets a `-N` suffix), and `put`s that relative path as the value. The engine stays
oblivious -- it stores a path like any other; the front-end (feed.c) owns blob
placement. `blobs/` is REAL DATA, not rebuildable -- as are `tomb`, `ktomb`, `katt`,
`mts` and `sts` (lose `sts` and a record that survived a peer's delete is simply
deleted again on the next sync; lose `katt` and a tag put back on is removed again by
the peer that removed it); only
`idx/`, `off`, `multi` and `next_id` can be rebuilt from `store`.
`find` searches the path, not the blob's contents (tags-only). `ais --where`
prints the index dir so a front-end can resolve `blobs/<timestamp>.txt`.

### Concurrency
Reads take no lock; each writer takes an exclusive `flock` on `INDEX/lock` for
the duration of one mutating op and reloads `next_id` under it, so concurrent
writers serialize without colliding on an id, and a long-lived reader
(`ais --serve`) never blocks the CLI. Full model: `LOCKING.md`.

## Module map (one concept per file -- see STYLE.md)

    common.h       shared limits/types (AIS_LINE_MAX, AIS_KEY_MAX, ...)
    key.c/.h       key encoding (lower; space, ctrl, | / \ -> '_') + the navigable prefix
    store.c/.h     append-only store: append/stream records, monotonic id,
                   resolve by id, value->id scan for idempotency
    post.c/.h      posting lists: append an id to a key's file, open a key's
                   ascending id stream (uses key.c for placement)
    merge.c/.h     the k-way streaming merge (AND/OR) over sorted id streams
    compact.c/.h   tombstones + compaction
    ais.c/.h       the public facade composing the above (ais.h is the API)
    embed.c/.h     in-process FFI seam (ais_embed_*) for Flutter / native hosts
    help.c/.h      usage_short / usage_long
    log.c/.h       die() (CLI fatal: stderr + exit) + debug() (runtime -d gated trace)
    main.c         CLI / getopt_long dispatch (recall is the default; -v/-k, --commands)
    tests.c        the test bundle (linear, inline, one comment per test)

## CLI

Flag-based so no tag is shadowed: a bare word is always a KEY (recall is the
default), a value is marked `-v`, a command is a `--word`. Dispatch: a `--CMD`
flag selects a command; else `-v`/`-i` mean store; else recall the keys.

    ais [-f DIR] [-o] KEY...           recall (AND; -o = OR)        <-- default
    ais [-f DIR] -v VALUE [KEY...]     store (-v - = stdin; repeat -v = multi-link)
    ais [-f DIR] -i [KEY...]           interactive: keys per piped line
    ais [-f DIR] KEY... -e             store ENCRYPTED (prompts value+passphrase; -v - = stdin; reveals on tty recall)
    ais [-f DIR] --find TEXT           search values by substring
    ais [-f DIR] --add ID -v VALUE
    ais [-f DIR] --set ID -v OLD -v NEW  replace ONE value in place (id, ts, keys kept)
    ais [-f DIR] --doc KEY... < FILE   save a multi-line document as a blob file
    ais [-f DIR] --doc KEY... -e < FILE  encrypt a whole document to an aisc: blob (--del/--del-key shreds it)
    ais [-f DIR] --untag KEY           remove the tag, KEEP every record (reversible)
    ais [-f DIR] --del-under KEY       DELETE every record filed under KEY
                                       (--del-key is the old name, kept as an alias)

The two are one keystroke apart and opposite in consequence, so every surface
states the difference in the same words rather than relying on the command name:
the safe one names the TAG, the destructive one names the RECORDS and their
count, and the destructive one lists what it would take before asking and points
at the safe one. The web GUI (`--serve`) offers the same pair on each tag row,
over `/api/untag` and `/api/del-under`; there the destructive path also requires
the tag name to be typed, because it destroys records that are not on screen,
while the safe one runs a 5s undo window instead of a modal (nothing reaches the
engine until it lapses). There are TWO web front ends over that one API -- the
page embedded in `c/serve.c` (the default) and `app/` (a PWA, served when
`$AIS_WEB` points at it) -- and they deliberately share element ids and function
names so `tests/gui/cdptest.c` drives both. A feature added to one and not the
other is a bug; the suite runs the same driver twice to catch it.
    ais [-f DIR] --del ID | --dump | --keys | --stats | --compact
    ais [-f DIR] --tags | --timeline       browse keys, or records newest-first
    ais [-f DIR] --import < FILE | --where | --project [KEY] | --serve [PORT]
    ais [-f DIR] --import-interactively   like --import, but y/N per record (answers on the tty)
    ais --switch [NAME]               switch the current index (no arg shows it; -c NAME [DIR] creates)
    ais --indexes                     list named indexes (* on current; 'home' = ~/.ais)
    ais --forget NAME                 drop a name from the registry (its data dir is left alone)
    ais --default [PATH]              DEPRECATED: the old single saved default (use --switch)
    ais --init                        create a local .ais here

INDEX location precedence (no env vars; `-f` is the only override): `-f/--index
DIR` > nearest `.ais/` (walking up, git-style) > the CURRENT named index from
`~/.ais/config` (set with `--switch`; falls back to the legacy `index = PATH`
line when there is no `current`) > `~/.ais` (the built-in "home" index, created
on first use).
No args -> usage_short to stderr, exit 2. `-h` -> usage_short. `--help` ->
usage_long.

### Named indexes (multi-index, git-branch-like)
`~/.ais/config` is a plain `key = value` file holding a registry of named
indexes plus a `current` pointer:

    current = work
    index.work = /home/me/work/.ais
    index.play = /home/me/.ais-play

`--switch NAME` repoints `current`; `--switch -c NAME [DIR]` registers a new
index (DIR default `~/.ais-NAME`), creates it, and switches; `--indexes` lists
them (`*` on the current); `--forget NAME` drops the registry entry (never the
data dir). The reserved name `home` is the built-in `~/.ais` -- never stored,
always present -- and `--switch home` returns to it. Indexes are SEPARATE
stores, so switching only repoints `current`: there is no history merge (move
records between indexes with `--import` / `--import-interactively`). The legacy
`index = PATH` line (the old `--default`) is still honoured when there is no
`current`, for one release. Resolution lives in `locate.c`; the config layer
(`config_get`/`config_set`) and the registry calls (`ais_current_*`,
`ais_index_*`) are there too. `ais_home_override()` relocates the config home
(the test seam, and an embedder hook).

## Implementation order

1. common.h + key (encode, prefix) + store skeleton (open/close, next_id
   read/recover, INDEX dir + lock).               test: key encode/prefix.
2. ais_put (append store, bump next_id, append to each key's posting; idempotent
   store-scan).                                    test: ids monotonic, postings
                                                   created & ascending, idempotent.
3. merge + ais_get (AND/OR) + ais_record.          test: AND/OR over the fixture.
4. ais_add (continuation line) + multi-value record. test: multi-link.
5. ais_del (tomb) + suppression in get/dump.       test: delete semantics.
6. ais_compact.                                    test: space reclaimed.
7. main.c (getopt_long: recall-default, -v/-k values+keys, --commands, help).
8. tests.c full bundle against tests/INDEX.

1 -> 2 -> 3 are sequential (the read path needs the write path's sorted output).
