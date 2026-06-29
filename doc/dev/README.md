# doc/dev: for developers and contributors

Short technical notes for anyone building on or contributing to AIS. The
public-facing docs live one level up in `doc/` (about, overview, migration,
provenance); this directory is implementation detail.

Core format and code:

- **LAYOUT.md**: on-disk format and the module map (how the store is organized).
- **BNF.txt**: the formal storage grammar a writer must produce and a verifier checks.
- **STYLE.md**: coding ideology: C99, stack/streaming discipline, error idioms.
- **LOCKING.md**: the reader/writer lock model and `next_id` correctness.
- **WHY-PLAIN-TEXT.md**: why the text format is fast (1M-record measurements) and the case against "use a binary DB".

Sync (multi-device):

- **SYNC.md**: the sync model overview.
- **SYNC_PROTOCOL.md**: the wire/file protocol.
- **MERGE.md**: how two indexes reconcile.

Distribution and front-ends:

- **DISTRIBUTION.md**: one download per platform, packaging, the GUI inventory, and the planned phone PWA.
- **SIGNING.md**: Windows (SignPath) and Android (keystore) release signing.
- **GUI.md**: shared label/layout conventions across the web, Flutter, and Win32 front-ends.

Start with `../../AGENTS.md`, then `LAYOUT.md` and `STYLE.md`.
