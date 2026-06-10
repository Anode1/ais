# doc/dev — for developers and contributors

Short technical notes for anyone building on or contributing to AIS. The
public-facing docs live one level up in `doc/` (about, overview, migration,
provenance); this directory is implementation detail.

- **LAYOUT.md** — on-disk format and the module map (how the store is organized).
- **STYLE.md** — coding ideology: C99, stack/streaming discipline, append-only.
- **LOCKING.md** — the reader/writer lock model and `next_id` correctness.
- **WHY-PLAIN-TEXT.md** — why the text format is fast (1M-record measurements) and the case against "use a binary DB".

Start with `../../AGENTS.md`, then `LAYOUT.md` and `STYLE.md`.
