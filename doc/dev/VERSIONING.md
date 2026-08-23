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

## The on-disk format is versioned separately

`AIS_FORMAT_VERSION` (`c/common.h`) is written to `INDEX/version`. `store_open`
refuses an index newer than it understands, loudly, rather than misreading it,
and stamps an older one forward.

Data outlives code, so this number moves only when the canonical store line
changes shape — not when the library or the release moves. It is currently 4
(v1 had no `ts`, v2 added a local `ts`, v3 made it UTC with a trailing `Z`, v4
added the per-record clocks `mts`/`sts`/`katt` that decide what a delete means).
Blob names carrying a random tag did NOT move it: the store line keeps its shape,
a longer string in the value field reads correctly on any binary, and bumping
would lock un-upgraded devices out of a shared index directory for no gain.

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

## Cutting a release

The tag is the release. Everything below exists because the tag is also what
four other files quote, and a tag pushed before they agree ships a build that
names the previous version.

**1. Gate.** `make ut` green, then `make codeut-asan` and `make codeut-ubsan`
(the pre-push hook and `sanitizers.yml` run the last two, `make hooks` installs
the hook). Build from a clean tree: `AIS_VERSION` is stamped at compile time, so
an object file left from before the tag keeps the old string and `make` has no
reason to relink. `make clean` first, then check `./ais --version`.

**2. The bump commit,** `release: vX.Y.Z`, moving the four places that carry the
number in text:

| File | What |
| --- | --- |
| `app/flutter/lib/version.dart` | the `APP_VERSION` / `APP_BUILD` defaults |
| `app/flutter/pubspec.yaml` | `version: X.Y.Z+BUILD` |
| `doc/dev/PACKAGING.md` | the `make AIS_VERSION=` line and the tarball URL |
| `packaging/aur/PKGBUILD` | `pkgver` |
| `doc/ROADMAP.md` | the "Known gaps, as of" heading and the release chores under it |

`flutter.yml` fails the build if the first two disagree with each other. Nothing
checks the other two.

**3. The tag,** annotated, its body in three sections: FIX (what was broken),
PARITY (what one front end gained that another already had), DESIGN (what
changed on screen). `git push --follow-tags`.

**4. What the tag does.** `release.yml` builds and publishes: Linux x86_64 and
arm64, macOS arm64, each a zip plus `.sha256`, and the Android `.apk` and `.aab`.
Both workflows pin Flutter deliberately (currently 3.44.1); raise that pin and
`android/`'s Gradle wrapper together, never one alone. iOS is not in the release
matrix and cannot be until the app is signed (issue #1).

**The artifacts are stamped from the tag, not from `git describe`.** Describe adds
`-dirty` whenever anything in the checkout has been touched, and the suite the
release job runs resolves Dart packages, which can rewrite a tracked lockfile.
v0.3.19 shipped as `ais 0.3.19-dirty`, in a directory named after it, so its
binaries could not name the commit they came from. `release.yml` passes
`AIS_VERSION` from `GITHUB_REF_NAME`, `scripts/dist.sh` honours it, and the
Flutter layer now resolves with `--enforce-lockfile` so a moved transitive
version fails the build instead of quietly editing a tracked file.

**If the tagged build fails,** fix the cause, move the tag onto the fix commit
(`git tag -f`, `git push -f origin vX.Y.Z`) and let it rebuild. Both v0.3.17 and
v0.3.19 needed that, which is the reason the toolchain is pinned at all. Check
the run rather than assuming: `gh run list --workflow=release.yml`.

**5. What stays manual.** Uploading the `.aab` to Play (`ANDROID_RELEASE.md`),
and the AUR: the reference `PKGBUILD` moves with this repo, the copy users
install lives in the AUR repository and needs the same `pkgver`, `pkgrel=1` and a
regenerated `.SRCINFO`.
