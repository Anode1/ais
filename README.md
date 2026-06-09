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
| `doc/STYLE.md` | Coding style and ideology: C99, stack/streaming discipline, append-only sharded storage. |
| `tests/INDEX/store` | A small sample index — the test fixture and a worked example of the store format. |

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

## Design philosophy

Performance is traded for **universality, robustness, and longevity** — the store is built to outlive its own tools.

- **Plain text over binary.** A binary index (the 2009 Lucene release) is fast but fragile, and structured text is no safer: one missing brace makes a JSON or XML document unparseable in full. Line-oriented plain text degrades *gracefully* — a damaged byte costs a character or a line, and natural-language content is redundant enough to reconstruct the rest. The honest claim is *locality*: corruption stays local and the store stays usable, not that any single identifier self-heals.
- **Compression at the key, redundancy in the file.** A short key addressing a body of content is the compression; the file itself stays redundant on purpose. They sit at different layers, so robustness and compression never compete.
- **Reference, not copy.** A record points to content by URI; data is never duplicated and may be read in place, even from a shared, read-only store.
- **Associative index, not a hierarchy or a database.** Encoding keys into directory names is a hierarchical database (filesystems descend from the IBM hierarchical model); it forces one tree per item and endless reorganizing. Here a record carries many keys and is retrieved by their algebra (union/intersection). A full RDBMS (a reference table in Oracle) is the same idea but overkill for one person; later filesystems (ReiserFS, XFS) improved storage without making keys-as-indices-with-algebra easy.
- **A personal prior — shareable but forkable.** One person's index is their own ordering of the world (their bias). It can be handed to another as a map into an unfamiliar tree — *read these first, in this order* — transmitting accumulated knowledge cheaply, but the recipient adapts it rather than inheriting an imposed ontology. The tool preserves a plurality of priors; it does not replace many with one.
- **Human-curated, not model-rewritten.** A machine asked to recompress the store optimizes its own objective and drops what the keeper marked essential — it cannot tell, from its own context, which items are load-bearing rather than restatable. So the ledger is curated by human decision, never silently rewritten by the model. (This failure, observed while recompressing an early ledger, motivates the author's open call for reserved keys — "strong words" — that a recompressor must preserve verbatim.)
- **Versioned evolution.** Kept under version control, the index lives its own life: branches and merges are cheap variation and recombination, and the history records its compression over time. Version control supplies the cheap half (variation); what survives is still chosen by people and use.

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
