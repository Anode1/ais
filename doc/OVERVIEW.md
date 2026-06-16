# AIS — Overview: design, status, provenance

What AIS is and isn't, in brief: [`about.txt`](about.txt).
The on-disk format and module map: [`LAYOUT.md`](dev/LAYOUT.md).
This document is the rationale and history behind those.

What it is, in one phrase: an extension of your **associative memory** — the *memex* Vannevar Bush described in 1945 but never built, here as plain text you own.

Design in one line: an **immutable content store** plus a small, **rebuildable key index**.

GUIs available: a local **web app** (`ais --serve`), a **Tk desktop app**, and an installable **PWA** — all thin clients over the one CLI/engine.

## Repository layout

| Path | What |
|------|------|
| `c/` | The **ANSI C / C99 reference implementation** — the current tool. |
| `legacy/` | The preserved original project: the **2005 shell scripts** (`ais-scripts`, the earliest implementation) and the **2009 Java/Lucene** release, plus screenshots and SourceForge metadata. |
| `doc/history/PRIORITY.md` | Authorship and provenance record (priority trail, 2001 → 2026) — frozen deposit, [doi.org/10.5281/zenodo.20647048](https://doi.org/10.5281/zenodo.20647048). |
| `doc/dev/STYLE.md` | Coding style and ideology: C99, stack/streaming discipline, append-only sharded storage. |
| `tests/INDEX/store` | A small sample index — the test fixture and a worked example of the store format. |
| `example/` | A ready-to-use **sample index**: `ais -f example docs`, or read `example/store` by hand. Models correct usage — links (paths, URLs) and short notes, **not** stored documents. |

## Design philosophy

Performance is traded for **universality, robustness, and longevity** — the store is built to outlive its own tools.

- **Plain text over binary.** A binary index (the 2009 Lucene release) is fast but fragile, and structured text is no safer: one missing brace makes a JSON or XML document unparseable in full. Line-oriented plain text degrades *gracefully* — a damaged byte costs a character or a line, and natural-language content is redundant enough to reconstruct the rest. The honest claim is *locality*: corruption stays local and the store stays usable, not that any single identifier self-heals.
- **Compression at the key, redundancy in the file.** A short key addressing a body of content is the compression; the file itself stays redundant on purpose. They sit at different layers, so robustness and compression never compete.
- **Reference, not copy.** A record points to content by URI; your files stay where you keep them, in your own organization, never copied, moved, or read into the store. Data is never duplicated and may be read in place, even from a shared, read-only store — so your storage stays immutable and you can lay several independent indexes over the same untouched files. (Short notes and `--doc` text are the exception: small content kept inside, by choice.)
- **Associative index, not a hierarchy or a database.** Encoding keys into directory names is a hierarchical database (filesystems descend from the IBM hierarchical model); it forces one tree per item and endless reorganizing. Here a record carries many keys and is retrieved by their algebra (union/intersection). A full RDBMS (a reference table in Oracle) is the same idea but overkill for one person; later filesystems (ReiserFS, XFS) improved storage without making keys-as-indices-with-algebra easy.
- **A personal prior — shareable but forkable.** One person's index is their own ordering of the world (their bias). It can be handed to another as a map into an unfamiliar tree — *read these first, in this order* — transmitting accumulated knowledge cheaply, but the recipient adapts it rather than inheriting an imposed ontology. The tool preserves a plurality of priors; it does not replace many with one. **Averaging is not the same as holding a prior:** pooling many people's memories keeps only the shared component and cancels each person's individual *prior* — a systematic lens (an inductive bias), not noise — the very thing that made each one a point of view. A model trained on everyone gives you the average; only you hold your prior. So AIS keeps your prior unaveraged — to read yourself, or to feed an AI agent *on your terms* (your bias layered on the model's base, not replaced by the crowd's). (The prior-as-inductive-bias argument: Gavrilov, *Intelligence Is the Discovery of Compressors*, 2026 — Zenodo [doi.org/10.5281/zenodo.20440111](https://doi.org/10.5281/zenodo.20440111), lighter version at [gavr144.substack.com](https://gavr144.substack.com).)
- **Human-curated, not model-rewritten.** A machine asked to recompress the store optimizes its own objective and drops what the keeper marked essential — it cannot tell, from its own context, which items are load-bearing rather than restatable. So the ledger is curated by human decision, never silently rewritten by the model. (This failure, observed while recompressing an early ledger, motivates the author's open call for reserved keys — "strong words" — that a recompressor must preserve verbatim.)
- **Versioned evolution.** Kept under version control, the index lives its own life: branches and merges are cheap variation and recombination, and the history records its compression over time. Version control supplies the cheap half (variation); what survives is still chosen by people and use.

## Status

v0.1 — the C core is working and tested (`make check`: the C unit tests plus the
end-to-end CLI/streaming suite, all green). Implemented: key algebra (AND/OR over
sorted-unique posting sets), `put`/`add`/`del`/`dump`/`find`/`import`/`doc`/`init`,
`timeline` (recent records by save time, dateless first) and `tags` (every key by
record count) over a per-record timestamp column, an id→offset fast path for large
stores, an append-only immutable store with
confirmation-guarded deletes, multi-link records, UTF-8 keys, and a local web GUI
(`ais --serve`). The store is plain text and the whole index rebuilds from it
(`compact`); the CLI is the contract, and the GUIs are thin wrappers over it.

## Provenance

Conceived as a by-hand filesystem index (~2001), registered on SourceForge **2004-11-22**,
first implemented as shell scripts (**2005**, `legacy/ais-scripts/`), then as a Java/Lucene
web app (**2009**, `legacy/ais/`), and re-engineered from scratch in ANSI C (**2026**, `c/`).
Full trail in [`history/PRIORITY.md`](history/PRIORITY.md) — deposited, citable record: [doi.org/10.5281/zenodo.20647048](https://doi.org/10.5281/zenodo.20647048).
