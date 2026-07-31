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
      blobs/<timestamp>.txt  documents saved by `doc` (real data; not rebuildable)
      lock            writers' advisory flock (per op; reads lock-free)

### store -- the source of truth (append-only)
`id|ts|keys|value`. `id` is a positive integer, assigned monotonically. `ts` is
the save time, written on every put as UTC to the second, `YYYY-MM-DDThh:mm:ssZ`
(one canonical instant across devices). `keys` is one or more space-separated
encoded keys. `value` is the literal resource: a URL, URI, absolute path, or a
path relative to INDEX (relative keeps the whole INDEX portable). Because ids are
monotonic, the store is physically in id order.

**The keys field is authoritative, and every key-attach must reach it.** The store
is the source of truth and `idx/` is a rebuildable accelerator, so a key that lives
only in a posting list does not exist as far as the store is concerned: compaction
rebuilds `idx/` from the store lines and silently drops it, `--dump` prints the
stale field, and `--export`/`--import` never carry it. Attaching a key therefore
rewrites the record's lines in place (all of them: every line of a multi-line
record repeats the same keys field), the same rewrite compaction and
`ais_set_value` perform. This is the one exception to append-only besides
compaction and the in-place value edit; it is cheap because it is rare (a re-put
with new keys, or `--update`), and the alternative -- a durable "key added" file
mirroring `ktomb` -- would add a fourth store file and a merge-stream type to
express something the keys field already says. Attach is folded first (`|` and
control bytes to `_`, as `keys_attach_only` does for a put): a stored key shares
the line's `|` delimiter, so a raw `a|b` would shift the value into the wrong
field, and a raw newline would end the line and drop the value.

Detach is NOT symmetric. It removes the posting and records a `ktomb` entry, and
the key stays in the keys field until compaction strips it (`compact_visible_keys`).
So the keys field is the authoritative record of what has been ATTACHED, not the
live key set: a detached-but-not-yet-compacted key is still in it, and `ktomb` is
what makes the removal true and lets it propagate to peers (I1). An attach needs no
tombstone because the rewritten keys field propagates on its own in the `A|` line.

**A record may have NO keys.** `untag KEY` leaves one behind whenever `KEY` was a
record's only key, and the store represents that as an empty keys field. Such a
record is still live: it keeps its id and value and answers `--find`, `--timeline`
and `--dump`; it is simply not filed under anything. Anything that reads a record
line must carry it through -- `import` accepts an empty keys field on a `--dump`
line (which has an id to vouch for it) precisely so that `--dump | --import` does
not lose exactly those records. A hand-written `|value` line has no id and is
still refused as a typo.

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
identity: `dump` stays `id|keys|value` and `import` re-stamps each line with the
import time, exactly as it reassigns ids. The date is read only for the
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
NAVIGABLE prefix of the key (first one or two encoded chars): `idx/a/apple`.
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
id k's first `store` line. `ais_record(id)` seeks straight there, and re-checks
the line's id, so a stale offset never returns wrong data -- it just falls back
to the scan. `off` is maintained in lockstep by `put` (append) and rebuilt by
`compact` (with `0` sentinels for the gaps that dropped ids leave).

`multi` lists ids carrying more than one value line (from `add`, whose
continuations are scattered); the fast path skips these and scans, so multi-value
records are always read in full. Both files rebuild from `store` -- delete them,
`compact`, and nothing is lost.

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
converting anything -- old tombstones keep working, `--forget-deleted` retires
them when the user wants, and every new delete is salted. The one thing that must
never happen is a peer computing a digest the other cannot reproduce, which is
why the salt is per-record data both sides already hold rather than a shared key.

**One deliberate asymmetry: an ENCRYPTED blob is shredded at delete time, not at
compaction.** So a deleted plain record is recoverable until `compact` (truncate
`tomb`), while a deleted secret is gone immediately -- its ciphertext is zero-filled
and unlinked by `secret_shred_blob` before the tombstone is written. Deferring the
shred to compaction would make the two uniform, but at the cost of leaving deleted
ciphertext on disk for however long the user waits to compact, and exportable to a
peer in the meantime. Destroying a secret promptly is worth more than a tidy rule,
so this stays. The consequence to know: "deleted, recoverable until compaction" is
true of ordinary records and NOT of encrypted ones. Do not tell users a delete is
undoable without saying which kind.

### Idempotent put -- by store scan, no index, no hash
`put(keys, value)`: find whether `value` is already stored by streaming `store`
and comparing the value field (the store IS the value->id map). If found, reuse
that id and add any new keys to its posting lists. If new: id = next_id++,
append the store line, append id to each key's posting list. Identical re-puts
change nothing. (O(n) per put; fine at personal scale -- bulk-indexing a very
large directory is the one case that degrades, acceptably.)

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
reads a document from stdin, writes it to `blobs/<timestamp>.txt` (named by local
time, so `ls blobs/` reads chronologically; a same-second doc gets a `-N`
suffix), and `put`s that relative path as the value. The engine stays
oblivious -- it stores a path like any other; the front-end (feed.c) owns blob
placement. `blobs/` is the only REAL DATA besides `store` (not rebuildable);
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
