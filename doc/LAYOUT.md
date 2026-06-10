# AIS -- On-disk layout and module map

The contract the implementation honours. See `doc/STYLE.md` for the coding
ideology (C99/Robbins, stack/streaming, append-only, modular). Nothing in AIS
is hashed: every file is plain text, readable, greppable, repairable by hand.

## An INDEX is a directory

    INDEX/
      store           append-only records:  id|keys|value     (one per line)
      next_id         a single line: the next id to assign
      idx/<p>/<key>   posting list for a key: ids, one per line, ascending
      off             id->offset accelerator: line k = byte offset of id k
      multi           ids carrying >1 value line (from add)
      tomb            tombstones: deleted ids, one per line
      lock            single-writer advisory lock (flock)

### store -- the source of truth (append-only)
`id|keys|value`. `id` is a positive integer, assigned monotonically. `keys` is
one or more space-separated encoded keys. `value` is the literal resource: a
URL, URI, absolute path, or a path relative to INDEX (relative keeps the whole
INDEX portable). Records are only appended, never rewritten except by
compaction. Because ids are monotonic, the store is physically in id order.

A record may hold several values (multi-link): `add` appends another
`id|keys|value` line with the same id. `ais_record(id)` resolves a single-value
record by one seek via `off` (below); a multi-value id falls back to a store
scan that collects every line bearing that id.

### next_id -- monotonic id counter
Holds the next id as text. On put: read, use, write back +1. If missing
(hand-edited store, first run), recovered by one streaming pass over `store`
taking max(id)+1 -- bounded memory, one `long`.

### idx/<p>/<key> -- posting lists, sorted by construction
`<key>` is the encoded key (lowercased; spaces/tabs -> `_`). The file is that
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

### Concurrency
Single-writer: `ais_open` takes an advisory `flock` on `INDEX/lock`. One writer
at a time; the append-only design leaves readers unaffected. Concurrent writers
are out of scope for v1.

## Module map (one concept per file -- see STYLE.md)

    common.h       shared limits/types (AIS_LINE_MAX, AIS_KEY_MAX, ...)
    key.c/.h       key encoding (lower, ' '->'_') + the navigable prefix
    store.c/.h     append-only store: append/stream records, monotonic id,
                   resolve by id, value->id scan for idempotency
    post.c/.h      posting lists: append an id to a key's file, open a key's
                   ascending id stream (uses key.c for placement)
    merge.c/.h     the k-way streaming merge (AND/OR) over sorted id streams
    compact.c/.h   tombstones + compaction
    ais.c/.h       the public facade composing the above (ais.h is the API)
    help.c/.h      usage_short / usage_long
    log.c/.h       die() (CLI fatal: stderr + exit) + debug() (runtime -d gated trace)
    main.c         CLI / getopt_long dispatch (get is the default verb)
    tests.c        the test bundle (linear, inline, one comment per test)

## CLI

get is the default: bare positional args are query keys; mutations are explicit
verbs. Dispatch: if argv[1] is a known verb (put/add/del/dump/compact/get) run
it; otherwise treat all args as get keys.

    ais [-f DIR] [-o] KEY...        get (AND; -o = OR)        <-- default
    ais [-f DIR] put VALUE KEY...   put            (-R DIR: index a whole folder)
    ais [-f DIR] add ID VALUE
    ais [-f DIR] del ID
    ais [-f DIR] dump | compact

INDEX location precedence: `-f/--index DIR` > `$AIS_INDEX` > nearest `.ais/`
(walking up, git-style) > `$XDG_DATA_HOME/ais` (else `~/.local/share/ais`).
No args -> usage_short to stderr, exit 2. `-h` -> usage_short. `--help` ->
usage_long.

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
7. main.c (getopt_long, get-default, verbs, help, put -R).
8. tests.c full bundle against tests/INDEX.

1 -> 2 -> 3 are sequential (the read path needs the write path's sorted output).
