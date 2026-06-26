# AIS -- On-disk layout and module map

The contract the implementation honours. See `doc/STYLE.md` for the coding
ideology (C99/Robbins, stack/streaming, append-only, modular). Nothing in AIS
is hashed: every file is plain text, readable, greppable, repairable by hand.

## An INDEX is a directory

    INDEX/
      store           append-only records:  id|ts|keys|value  (one per line)
      next_id         a single line: the next id to assign
      version         on-disk format version (2 = the ts column; see below)
      idx/<p>/<key>   posting list for a key: ids, one per line, ascending
      off             id->offset accelerator: line k = byte offset of id k
      multi           ids carrying >1 value line (from add)
      tomb            tombstones: deleted ids, one per line
      blobs/<timestamp>.txt  documents saved by `doc` (real data; not rebuildable)
      lock            writers' advisory flock (per op; reads lock-free)

### store -- the source of truth (append-only)
`id|ts|keys|value`. `id` is a positive integer, assigned monotonically. `ts` is
the save time, local, `YYYY-MM-DDThh:mm:ss` (written on every put). `keys` is
one or more space-separated encoded keys. `value` is the literal resource: a
URL, URI, absolute path, or a path relative to INDEX (relative keeps the whole
INDEX portable). Records are only appended, never rewritten except by
compaction. Because ids are monotonic, the store is physically in id order.

**Format versions.** v1 was `id|keys|value` (no `ts`); v2 adds the `ts` column.
`INDEX/version` records the format, and `store_open` upgrades v1->v2 in place
the first time this build touches the index (a v1 `ais` then refuses it, rather
than misread a `ts` as keys). The parser reads BOTH shapes per line: it treats
field 2 as `ts` only if it looks like a date (`YYYY-MM-DD`, optionally
`...Thh:mm[:ss]`); anything else -- including a *year used as a key* like
`2026` -- stays the keys field. So a missing, empty, or malformed date never
loses a record: the line reads as a dateless v1 record (id, keys, value intact),
and the timeline surfaces such records FIRST rather than dropping them.
Timestamps are NOT identity: `dump` stays `id|keys|value` and `import`
re-stamps each line with the import time, exactly as it reassigns ids.
The date is read only for the timeline/tags views -- the recall path never
parses it beyond the trivial field-split. (Pre-v2 archives carry no `ts`; the
filesystem mtime of `store`/`idx` files is the only date such records have, and
it is reset by compaction or a copy that does not preserve times.)

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
with `a`. No hashing: keys are human words, kept as themselves. (If a prefix
bucket ever grows large, split it adaptively by the next character.)

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
present in `tomb`). Physical removal happens only at compaction.

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
    ais [-f DIR] --doc KEY... < FILE   save a multi-line document as a blob file
    ais [-f DIR] --doc KEY... -e < FILE  encrypt a whole document to an aisc: blob (--del/--del-key shreds it)
    ais [-f DIR] --del ID | --del-key KEY | --dump | --keys | --stats | --compact
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
