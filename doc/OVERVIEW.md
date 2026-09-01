# AIS: design rationale and provenance

What AIS is and isn't, in brief: [`about.txt`](about.txt).
The on-disk format and module map: [`LAYOUT.md`](dev/LAYOUT.md).
This document is the rationale and history behind those.

What it is, in one phrase: an extension of your **associative memory**, a working *memex* as plain text you own. See [`about.txt`](about.txt) for the memex origin and pitch.

Design in one line: an **immutable content store** plus a small, **rebuildable key index**.

Where the code lives: [`dev/LAYOUT.md`](dev/LAYOUT.md) for the module map, [`../AGENTS.md`](../AGENTS.md) for the directories. What has shipped and what is next: [`ROADMAP.md`](ROADMAP.md). Which front end each platform gets: [`dev/DISTRIBUTION.md`](dev/DISTRIBUTION.md).

## Design philosophy

Performance is traded for **universality, damage tolerance, and longevity**: the store is built to outlive its own tools.

- **Plain text over binary.** A binary index (the 2009 Lucene release) is fast but fragile, and structured text is no safer: one missing brace makes a JSON or XML document unparseable in full. Line-oriented plain text degrades *gracefully*: a damaged byte costs a character or a line, and natural-language content is redundant enough to reconstruct the rest. The claim is *locality*: corruption stays local and the store stays usable. No single identifier self-heals.
- **Compression at the key, redundancy in the file.** A short key addressing a body of content is the compression; the file itself stays redundant on purpose. They sit at different layers, so damage tolerance and compression never compete.
- **An index of references.** A record points to content by URI; the index is a view and your files are never touched, so you can lay several independent indexes over the same untouched store. See [`about.txt`](about.txt) for the full statement.
- **An associative index.** Encoding keys into directory names is a hierarchical database (filesystems descend from the IBM hierarchical model); it forces one tree per item and endless reorganizing. Here a record carries many keys and is retrieved by their algebra (union/intersection). A full RDBMS (a reference/lookup table) is the same idea but overkill for one person; later filesystems (ReiserFS, XFS) improved storage without making keys-as-indices-with-algebra easy.
- **A personal prior, shareable but forkable.** One person's index is their own ordering of the world (their bias). It can be handed to another as a map into an unfamiliar tree, *read these first, in this order*, but the recipient adapts it rather than inheriting an imposed ontology: the tool preserves a plurality of priors rather than replacing many with one. The "a model trained on everyone gives you the average; only you hold your prior" argument lives in [`foundation.md`](foundation.md).
- **Human-curated, not model-rewritten.** A machine asked to recompress the store optimizes its own objective and drops what the keeper marked essential: it cannot tell, from its own context, which items are load-bearing rather than restatable. So the ledger is curated by human decision, never silently rewritten by the model. (This failure, observed while recompressing an early ledger, motivates the author's open call for reserved keys, "strong words", that a recompressor must preserve verbatim.)
- **Versioned evolution.** Kept under version control, the index lives its own life: branches and merges are cheap variation and recombination, and the history records its compression over time. Version control supplies the cheap half (variation); what survives is still chosen by people and use.

## Provenance

Conceived as a by-hand filesystem index (~2001), registered on SourceForge **2004-11-22**,
first implemented as shell scripts (**2005**, `legacy/ais-scripts/`). Early C and Java editions
ran on Berkeley DB / Sleepycat (**2005-2007**) before Lucene; the Java/Lucene web app was
running by **2007** and published (after a delay) in **2009** (`legacy/ais/`). Re-engineered
from scratch in ANSI C (**2026**, `c/`).
Full trail in the deposited, citable record: [doi.org/10.5281/zenodo.20647048](https://doi.org/10.5281/zenodo.20647048).
