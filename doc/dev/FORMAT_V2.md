# The dump/import format, and hiding ids

Decision record. Implementation waits until after the Play closed test, because
this changes a documented interface.

Three independent audits, one per surface (engine, CLI, front ends), each denied
sight of the others' findings. They agreed on the verdict and disagreed on one
load-bearing fact, which is the reason for the split.

## The decision

`--dump` and `--import` use the CLI's own grammar, one record per line:

    KEY... -v VALUE

`-v` (or `--value`) takes **the rest of the line, verbatim**. No tokenising, no
quoting, no escaping, because no shell is involved. Values may contain `|`, `#`,
quotes, backslashes, anything but a newline.

No id. No timestamp. No field counting. Hand-written and dumped lines are the
same shape, which is what removes the ambiguity rather than marking around it.

## Why the old format could not be repaired

`--dump` emitted `id|keys|value` and `--import` accepted `keys|value` too, so
`a|b|c` was undecidable. The parser guessed by asking whether field 1 was all
digits. That guess is wrong in both directions:

- `2024|my note|http://x` -- a hand-written year tag, read as an id and lost
- `work|my note|http://x` -- read as keys + a value containing a `|`

Same shape, opposite rules. And the guess corrupts a **documented** feature:
`help.c:113` teaches year tags (`ais -v photo.jpg italy venice 2023`), and

    printf '2023|http://a|b\n' | ais --import

stores key `http:__a`, value `b`. The year is gone and no error is printed.

Every alternative considered kept the guess and paid for it:

- **a timestamp in `--dump`** identifies dump lines, but only that. It does not
  address keys-versus-value at all, and it makes every line longer forever.
- **a different delimiter** relocates the problem. `#` is in URLs, comments and
  markdown; any character is in someone's value.
- **escaping** taxes every read and write to fix one entry path, and costs the
  property the project exists for: you can no longer read a value by looking at
  it.
- **one key per line** removes the nested separator convention (`|` between
  fields, space between keys inside a field), which is a real gain, but it does
  not disambiguate dump from hand-written, and it duplicates the value once per
  key.

## Migration

An old line is detectable with certainty: it contains `|` and no ` -v `, a shape
the new grammar cannot produce.

Read it best-effort and **prefer visible noise over silent loss**: the leading
field becomes a key rather than being discarded as an id. A real id becomes a
junk key `1` that nobody will search for; a real tag like `2024` survives.

One case stays imperfect and cannot be fixed: an old two-field line whose value
contains a `|` is indistinguishable from an old three-field line. It will read as
one key too many. Warn, do not guess silently.

## Ids: what the audits found

**They are device-local surrogates and always were.** `MERGE.md:18` is headed
"The core problem: ids are device-local"; `SYNC.md:8` says the id is "never
shared". The `--export` stream has no id field: `A|ts|keys|value`,
`D|ts|hash`, `M|ts|hash|value`. Cross-device identity is `content_hash` over the
value. A peer re-imports under fresh ids and reassembles multi-value records
correctly. Verified, not inferred.

So the whole data model is already expressible without ids, and `--export` is the
existence proof.

**They must remain internal.** Postings *are* ascending id files and the read
path is a numeric k-way merge over them. `off` seeks by `(id-1)*width`. `multi`,
`mts`, `sts`, `tomb`, `ktomb` slot-address by id. `next_id` must stay monotonic
and must never regress past a retained tombstone. Recency order is id-descending
because `ts` is non-unique, second-resolution, and absent on legacy lines.

**They can leave the command line.** Every consumer has a value-addressed
replacement, and two of the four are already redundant with `put`:

    ais --add ID -v NEW        ->  ais -v EXISTING -v NEW KEY...   (already works)
    ais --update ID KEY...     ->  ais -v VALUE KEY...             (attach half already works)
    ais --set ID -v OLD -v NEW ->  ais --set -v OLD -v NEW
    ais --del ID               ->  ais --del -v VALUE

## The blocker, found only because the audits were independent

The engine audit concluded "a value is already a primary key here", citing that
`put` dedups by value and `ais_set_value` refuses a value another record holds.

The front-end audit tested `--add` and demonstrated otherwise:

    1|one|shared-value
    2|two|other
    2|two|shared-value        <- ais --add 2 -v 'shared-value'

`add_link` (ais.c:722-766) is the only write path with **no** duplicate guard,
unlike `ais_put_at_k` (ais.c:372), `ais_merge_addval` (ais.c:1236) and
`ais_set_value` (ais.c:886). `store_find_value` then resolves that value to
record 1, so any value-addressed `--del` or `--set` would silently act on the
wrong record.

**Fix first, before anything is value-addressed:** give `ais_add` a duplicate
guard. That turns "a value is identity" from an almost-invariant into a real one.

Write it as **hash filter, strcmp confirm**, not as a plain scan:

    content_hash(new_value, h);        /* 16 hex chars */
    /* per store line: compare h first; on a hit, confirm with strcmp */

Comparing digests instead of whole values is what makes this cheap on `--doc`
blobs and long notes, and it is the same content-addressing the merge path
already uses (`mdel_seek`, ais.c:1187).

But note why the local guard confirms and the wire cannot. `mdel_seek` accepts a
hash match as proof because a delete arrives as `D|ts|hash` with no value: the
hash is all the evidence there will ever be. A local guard holds both, so a
`strcmp` on a hit costs nothing and removes the one bad outcome -- FNV-1a is
documented as "NOT a security hash" (store.c:170), and an unconfirmed collision
would reject a legitimate distinct value with "already exists".

## What ids carry that nothing else does

Two things, and both need a decision rather than a substitution.

**1. Multi-link grouping.** Recall prints one line per link. A record can hold
several. The shared id is the only thing saying "these are one record". Without
it, `ais trio` printing three lines is indistinguishable from three records.
Flutter's `record_rows.dart` records the acceptance failure this causes: a user
watches a row they did not touch vanish. The replacement is a **presentation
marker**, not another identifier -- an indent, a blank line between records, or
one row per record carrying a link count.

**2. Value-less records.** `main.c:92` prints `id|` for a posting that names an
id with no store line (a hand-edited or truncated index). Drop the id and the
line is empty, the diagnostic disappears, and such a record can never be named on
the command line, so `--del` cannot reach it.

## Front ends: deliberately unchanged

`serve.c` and `embed.c` keep using ids as internal handles, and that is correct
rather than a compromise. The API between the app and its own engine is **not a
device boundary**: both sides share one store, the id is minted and consumed
inside the same process, and it is never displayed. The rule being enforced is
that an id must not cross a boundary where two indexes could disagree, and this
is not one.

Making them value-addressed anyway would cost three things and buy nothing:

- `/api/get` and the FFI emit one line **per link**, so the id is the only
  grouping key; removing it forces one row per record with a link count
- paging is a keyset cursor on `after=`/`before=` id, and a value has no order,
  so it would need a server-issued opaque token -- which, if it wraps the id, has
  base64'd the problem rather than solved it
- `show_value` rewrites a document's stored `blobs/....txt` into
  `aisdoc:<base64>` on the way out, so a value-keyed delete from the page would
  match nothing in the store

So the scope is exactly: **the index keeps ids, sync never had them, the UI never
shows them, and `--dump` stops leaking them.**

## The empty-keys asymmetry disappears

Worth noting because it looked like a cost and is a gain. Today `import` accepts an
empty keys field only on a line with an id "to vouch for it" (LAYOUT.md), because
a hand-written `|value` is more likely a typo than a deliberate keyless record.

Under `KEY... -v VALUE` there is nothing to vouch for. A keyless record is `-v
VALUE`, and you cannot omit keys by accident the way you can leave a field empty
before a `|`. The marker makes intent explicit, so the special case, the voucher
and the typo heuristic all go away together.

## Order of work

1. ~~duplicate-value guard in `ais_add`~~ -- DONE 2026-08-03, folded into the pass
   add_link already made, hash filter with strcmp confirm, returns -2. Invariants
   pinned in tests/cli.sh under "wire:", "identity:" and "disposable:".
2. the new `KEY... -v VALUE` grammar for `--dump` and `--import`, sharing the
   CLI's parser, with old-format detection and a warning
3. value-addressed `--del`, `--set`, `--update`; drop or respell `--add`
4. a grouping marker for multi-link recall output
5. decide what replaces `put`'s id on stdout (13 test captures and any user
   script depend on it; `feed_stdin` already prints nothing, so silence has
   precedent)

## Related, and separate: blob STORAGE could be content-addressed

CORRECTION to an earlier reading of this file. `--doc` not deduping is not a gap:
`SYNC.md:12` settles it deliberately -- "Blob (`--doc`): identified by OCCURRENCE,
not content. Each doc is its own record." Filing the same file twice is meant to
give you two records. That is the identity rule and it stays.

What is separable is STORAGE. Identity by occurrence, storage by content: two
records keep their own identity while pointing at one file when the bytes match.
Today they cannot, because the value is a timestamp-derived path
(`blobs/2026-08-03-095035.txt`, doc.c:43-56):

    1|report |blobs/2026-08-03-095035.txt
    2|summary|blobs/2026-08-03-095035-2.txt     <- identical bytes

The same document filed under two tags cannot become one record with two keys,
which is what the value path does and what a user would expect.

Naming blobs by content hash would let identical bytes share one file, and buys:

- **sync could skip payloads the peer already holds.** The wire is
  `B|path|size` plus the bytes (feed.c:506-587). With a content-addressed name a
  peer recognising the hash can decline the transfer. Today it must take the
  bytes, because a timestamp path says nothing about what is inside.
- deleting one of two identical documents stops orphaning the other's storage
- an index moved between machines stops accumulating duplicate blobs

COST, and it is the one that matters: `--del` shreds the blob today
(`secret_shred_blob`). If two records can share a file, deleting one must not
destroy the other's storage, so this needs REFCOUNTING -- real complexity in a
project whose argument is subtraction. Also: blob filenames stop sorting
chronologically in `ls blobs/`, existing blobs keep their names so the directory
becomes mixed, and it is a store-format change.

Not scheduled, and the refcount is the reason. Recorded so the reasoning is not
rediscovered, and so nobody mistakes the occurrence rule for an oversight again.
