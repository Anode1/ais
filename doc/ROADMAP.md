# AIS: Roadmap

AIS is one small ANSI C engine (`c/`) with thin wrappers over a single FFI seam
(`embed.h`: `ais_embed_open` / `store` / `recall` / `timeline` / `tags` / …).
Almost everything below is a *wrapper* or a *packaging* task over that unchanged
engine, which is what keeps each piece tractable for one contributor at a time.
Help is welcome: open an issue to claim a piece.

## Shipped

- **Command line** (`ais`): Linux and macOS. (The Windows CLI is CI-validated but
  temporarily not published while the desktop GUI is reworked.)
- **Local web GUI** (`ais --serve`, 127.0.0.1 only): the default GUI on every desktop OS.
- **Android app** (Flutter over the C engine via the `embed.h` FFI seam): built and
  published each release as `.apk` (sideload) and `.aab` (Play bundle).
- **Documents as blobs** (`--doc`): a multi-line value is stored out-of-line under
  `blobs/` and recalled as its content.
- **Encrypted secrets** (`-e`): store a password or token encrypted inline (an opaque
  `aisc:` value; single-file ChaCha20-Poly1305 via monocypher in `c/crypto/`). Recall
  decrypts interactively; secrets are never emitted in plaintext by `--dump`.
- **Built-in LAN sync** (`c/sync.c`): one-way encrypted transfer (`--export --serve` /
  `--import <url> --token`) and two-way device sync (`--sync --serve` / `--sync <url>
  --token`) that converge in one round — end-to-end encrypted (XChaCha20-Poly1305 under a
  one-time token), LAN-only. See [`doc/SYNC.md`](SYNC.md).
- **Multiple named indexes** (`--switch` / `--indexes` / `--forget`) with a default
  project (`--project`).
- **Native Windows app** (`win32/`, pure Win32 over the engine): built and CI-validated,
  temporarily not published during the GUI rework.

## Planned

Roughly in priority order. These are now all UI or platform glue over the
unchanged engine: the two former engine-level items — LAN sync and encrypted
secrets — have shipped (see above).

### iPhone (iOS) · next up

The **Android** app has shipped (above); **iOS** is the next focus. The same
**Flutter** app (`app/flutter/`) runs over the C engine through the FFI seam
(`make lib` builds the shared library; `embed.h` is the contract). iOS needs a
native shell (App Store signing, and native speech later), but the engine already
compiles small and dependency-free, so the work is UI + platform plumbing, not
core changes. A browser **PWA** (`app/`) is a parallel, lower-friction track (see
[`dev/DISTRIBUTION.md`](dev/DISTRIBUTION.md) for the WASM/standalone plan).

### F-Droid (Android)

Publish the Android build on **F-Droid**, the free/open app store: a reproducible
build from source, no proprietary dependencies, plus the F-Droid metadata recipe.
Depends on the Android app above. Google Play is a separate, optional track.

### Speech support

Voice as a first-class input: **speak to file** (PUT) and **speak to recall**
(GET). On-device recognition where the platform provides it (iOS and Android
native speech APIs, not browser Safari, which is one reason iOS needs a native
shell). This is the seam toward the longer-horizon hands-free / wearable use.

### Native macOS app

A native macOS wrapper over the engine (as `win32/` is for Windows), so Mac users
get a real app, not only the web GUI via a launcher. A minimal AppKit/Swift shell
calling `embed.h`.

### Signing and notarization

So a *downloaded* build runs without security warnings. **macOS notarization**
(Apple Developer ID: `codesign` + `notarytool` + `staple` in CI) would remove the
"Apple could not verify 'ais' is free of malware" Gatekeeper block on downloaded
binaries, but it requires the paid Apple Developer Program ($99/year) and is not
planned. Meanwhile, clear the quarantine flag once with
`xattr -dr com.apple.quarantine .` (see the README), verify a download by its
SHA-256, or just build from source, which is never quarantined. **Windows code-signing** is already wired into the
release workflow (the SignPath OSS program), but is not active: SignPath's
Foundation program declined the project in June 2026 as too new: it gates on
community-adoption signals (stars, forks, third-party references) that a fresh
repo cannot yet show. Paid signing is not planned. The wiring stays in place;
reapply once the project has visible adoption. Until a build is signed, verify a
download by its SHA-256 or build from source (see the README).

## Not planned (non-goals)

- **A .NET / WinUI wrapper.** The native Win32 app (`win32/`) already covers
  Windows with no runtime dependency, and .NET's framework churn works against
  the "tiny, dependency-free, built to outlive its own tools" goal. Win32 is a
  decades-stable API; a self-contained .NET build drags a large runtime for no
  capability a user can feel.
- **A heavyweight backend** (SQLite, a database, a server daemon). Plain text is
  the durability and transparency guarantee, not a limitation, see the README
  "Questions."
- **A cloud account or sync service.** Sync is peer-to-peer over your own files
  (the built-in LAN sync under *Shipped*, or Syncthing; see [`doc/SYNC.md`](SYNC.md));
  nothing phones home, by design.

## How to contribute

- **Keep the core pure.** ANSI C lives in `c/`; platform code and any
  C++/Swift/Dart stays isolated in its own wrapper directory (`win32/`, `app/`,
  a future `macos/`).
- **Build the engine as a library:** `make lib`.
- **The contract is `embed.h`** (and the CLI). Wrappers call it; they never reach
  into the on-disk store format.
- Open an issue describing the wrapper or platform you want to take.

See [`dev/DISTRIBUTION.md`](dev/DISTRIBUTION.md) for the packaging plan and
[`dev/LAYOUT.md`](dev/LAYOUT.md) for the on-disk format and module map.
