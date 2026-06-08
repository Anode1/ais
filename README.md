# AIS — Associative Indexing Service

A personal, plain-text **associative index**: keys → content, an extension of human
associative memory ("a personalised Google over your own information"). You store a
resource (a URI or free text) under one or more keywords; you retrieve it by the
**union and intersection of keys**. A record can hold a **set of links** (a small graph
of related resources), and the whole store is human-readable plain text built to outlive
formats and tools.

Design in one line: an **immutable content store** plus a small, **rebuildable key index**.

## Repository layout

| Path | What |
|------|------|
| `c/` | The **ANSI C / C99 reference implementation** — the current tool. |
| `legacy/` | The preserved original project: the **2005 shell scripts** (`ais-scripts`, the earliest implementation) and the **2009 Java/Lucene** release, plus screenshots and SourceForge metadata. |
| `doc/PRIORITY.md` | Authorship and provenance record (priority trail, 2001 → 2026). |
| `testdata/seed.db` | A small sample index — unit-test fixture and a worked example of the store format. |

## Build and use (the C tool)

```sh
cd c
make            # builds ./ais   (also: `make ut` runs the unit tests; `make clean`)
```

```sh
# store a value (URI or text) under keys; prints the new record id
./ais put "/etc/samba/smb.conf" samba config network

# retrieve: AND of keys by default, OR with -o
./ais get samba config          # records having BOTH keys
./ais get -o samba nfs          # records having EITHER key

# attach more links to a record (multi-link): a record can hold several resources
id=$(./ais put "https://example.org/a" project docs)
./ais add "$id" "https://example.org/b"
./ais get docs                  # lists every link of each match, one per line

./ais del "$id"                 # delete a record by id
./ais dump                      # print all records
```

Options: `-f FILE` chooses the index file (default `ais.db`); `-o` makes `get` a union; `-h` for help.
The CLI reads `value key1 key2 …` from stdin too (`ais put -`), so it can be driven from a pipe or exec'd by another process.

## Store format

Plain text, one record per line, **pipe-delimited**:

```
id|keys|value1|value2|...|valueN
```

The pipe `|` is chosen for robust hand-editing: it is visually unambiguous (a tab looks like
spaces) and does not occur in well-formed values (a conformant URI encodes a literal pipe as
`%7C`). Keys within the keys field are space-separated; a record carries one or more values
(links). The on-disk index is fully rebuilt from this file on open, so the file is the source
of truth and the index is disposable.

## Status

v0 — the C core is working and tested (`make ut`: 36 checks green): set algebra (AND/OR,
sorted-unique posting sets), `put`/`get`/`add`/`del`/`dump`, multi-link records, save/reload
persistence, and queries against the `seed.db` fixture. The `db` layer is a seam for a
pluggable backend; a **SQLite backend** (for lifetime-scale archives) and an optional **web
layer** are future work, deliberately deferred while the CLI is the product.

## Provenance

Conceived as a by-hand filesystem index (~2001), registered on SourceForge **2004-11-22**,
first implemented as shell scripts (**2005**, `legacy/ais-scripts/`), then as a Java/Lucene
web app (**2009**, `legacy/ais/`), and re-engineered from scratch in ANSI C (**2026**, `c/`).
Full trail in [`doc/PRIORITY.md`](doc/PRIORITY.md).

## License

New code (`c/`): GNU General Public License v2 or later (per source headers).
Legacy material (`legacy/`) is preserved under its original Apache License 2.0.
Author: Vasili Gavrilov (GitHub [Anode1](https://github.com/Anode1)).
