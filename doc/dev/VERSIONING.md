# Versioning

Four things version independently here, and confusing them is how a two-week-old
engine shipped inside the app with nobody noticing.

| What | Where | Breaks when |
| --- | --- | --- |
| The release | git tag (`v0.3.9`) | — |
| The library ABI | `AIS_VERSION` / `ais_version()` | the FFI changes |
| The on-disk format | `AIS_FORMAT_VERSION`, `INDEX/version` | a store line changes shape |
| The sync wire | `AIS_SYNC_PROTO`, `AIS_FRAME_VER` | a verb or a frame changes |

## One source of truth: the git tag

`c/Makefile` derives `AIS_VERSION` from `git describe --tags` and stamps it into
every translation unit with `-D`. A source copy with no `.git` falls back to
`0.0.0-dev`, which is visibly wrong rather than silently wrong.

`app/flutter/tool/version.sh` derives the Flutter build's version from the same
tag:

    flutter build appbundle --release $(sh tool/version.sh)

`--build-name` is the tag without the `v` (both stores require a plain dotted
number, so no `-dirty`/`-gSHA` can ride along) and `--build-number` is
`git rev-list --count HEAD`, monotonic on a single trunk, which is what Play
demands of `versionCode`. `pubspec.yaml` keeps a version line as a FALLBACK for
a bare `flutter run`; it is not the source of truth and will drift if trusted.

**DECIDED (v0.3.10): `versionCode` is `git rev-list --count HEAD`.** The first
release under this rule uploaded from the `v0.3.10` tag, in the 240s -- run
`tool/version.sh` at the tag for the exact number rather than trusting a figure
written down here, since every commit moves it. Play never accepts a
versionCode at or below one already uploaded, so this is one-way: every number
below the last upload is now spent, and the alternative (a small sequential
counter) is no longer available. Always build a release with the flags:

    flutter build appbundle --release $(sh tool/version.sh)

A flagless `flutter build` silently uses pubspec's fallback, which is a SMALLER
number than any real upload and will be rejected by Play -- or, worse, accepted
into a track you did not mean. Treat a release built without the flags as
unusable rather than trying to reconcile it.

## Compile-time vs runtime: the pair that catches a stale library

    AIS_VERSION        what you were COMPILED against   (ais.h)
    ais_version()      what you are RUNNING             (the loaded libais)
    ais_version_number()  the same as an integer: major*1000000 + minor*1000 + patch

This is the same pair SQLite exposes (`SQLITE_VERSION` / `sqlite3_libversion()`)
and zlib exposes (`ZLIB_VERSION` / `zlibVersion()`), and for the same reason:
a shared library is not necessarily the one you built against.

It exists because it already went wrong. The Flutter bundle linked a `libais.so`
from 15 July against engine sources from 30 July, the About screen showed no
version at all, and two acceptance testers filed bugs that had been fixed
a fortnight earlier. Nothing in the product could have told them.

Two defences, and both are wanted:

- **The build dependency** — the Flutter bundle must not be able to link a stale
  `libais.so` in the first place. This is the real fix.
- **The runtime check** — the app reads `ais_version()` and shows it. This is the
  backstop, because it fires for people who run the app rather than build it.

The Dart binding looks the symbol up **lazily, inside the method, in a
try/catch** rather than as a `static final`. An eager binding throws at field
initialisation and takes the whole class down when the symbol is absent, which
would make an old library a crash instead of a message. Absent means the About
screen says `engine: unknown`.

## The on-disk format is versioned separately, and must be

`AIS_FORMAT_VERSION` (`c/common.h`) is written to `INDEX/version`. `store_open`
refuses an index newer than it understands, loudly, rather than misreading it,
and stamps an older one forward.

Data outlives code, so this number moves only when the canonical store line
changes shape — not when the library or the release moves. It is currently 3
(v1 had no `ts`, v2 added a local `ts`, v3 made it UTC with a trailing `Z`).

Two things follow that are easy to get wrong:

- The stamp is applied **on open**, so the moment one binary touches a shared
  index directory, older binaries are locked out of it. That only bites a shared
  directory — no sync path transports `INDEX/version` — but it means "upgrade one
  device to try it" is not safe for a Syncthing'd index.
- Anything that changes the *encoding* of a field, not just its meaning, is a
  format change even if the line still has four parts. Appending a counter to the
  timestamp is the worked example: `store_looks_like_ts` requires `p[20]` to be
  `|` or NUL, so an older reader takes the timestamp for the keys field, every
  field shifts right, the value absorbs the real keys, its content hash changes,
  and the next compaction rebuilds `idx/` from the corrupted field and destroys
  the user's tags. One such line reaching one un-upgraded device is silent,
  permanent loss. See `MERGE.md`.

## The wire is versioned separately again

`AIS_SYNC_PROTO` and `AIS_FRAME_VER` (`c/sync.c`) gate the sealed payload and the
bundle frame. A mismatch fails loudly with "update both", which is the correct
shape: a sync that silently does less than asked is worse than one that refuses.

For the merge STREAM there is no version byte — extensibility comes from
`--import` refusing verbs it does not know (see `MERGE.md`). That refusal is a
prerequisite for ever adding one, and it is newer than the peers in the field, so
a new verb may only be WRITTEN a full release after the refusing build has
reached every device.

## Where the version is surfaced

- `ais --version`
- `/api/version` and the About line in both web front ends
- Flutter's About: `AIS 0.3.10 (249) · engine: … · index format: v3`, copyable,
  because the first thing a bug report needs is which of the four numbers moved.
