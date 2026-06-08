# AIS — original project (preservation)

Preservation snapshot of the original **AIS (Associative Indexing Service)** project,
formerly hosted on SourceForge: https://sourceforge.net/projects/ais/
(registered **2004-11-22**; last updated **2013-04-01**; status Alpha; admins `vgavrilov`, `kalessin_o`).

The new implementation is re-engineered separately (ANSI C core in `../c/`); this directory only
preserves the original material, across two generations: the 2005 shell scripts and the 2009
Java/Lucene release.

## Contents

- `ais-scripts/` (+ `ais-scripts-0.5.tar.gz`) — the **2005 shell-script generation** (`ais-scripts-0.5`):
  the earliest implementation, a filesystem-sharded plain-text `INDEX/` with `get`/`put`/`delete` shell
  verbs. CVS-tagged **2005-01-28** (handle `sourcer`); recovered from the SourceForge mirror.
- `ais/` — the 2009 source/release snapshot (62 files). Byte-identical to the 2009-08-20 binary
  release `ais-20090820-bin.zip`, which is therefore **not** stored separately.
- `screenshots/` — original UI screenshots (`1-4.jpeg`) plus a 2026 capture.
- `project-summary.md` — SourceForge project metadata (audience; languages C/Java/Unix Shell;
  categories; contributors; status; dates).
- `sourceforge-metadata.json` — the authoritative SourceForge project description.
- `manifests/` — `legacy-file-manifest.txt` (dated inventory preserving the original 2006-2009 file
  timestamps) and `downloads-sha256.txt` (SHA-256 of the original release zip).
- `NOTICE.md` — original authorship/licensing notice.

The full SourceForge *project page* (HTML) is not kept here (it was auto-crawl scaffolding); the live
page is preserved independently at the Internet Archive (2022-05-13):
http://web.archive.org/web/20220513005530/https://sourceforge.net/projects/ais/

## License

Open source in continuity with the original SourceForge project (Apache License 2.0).
