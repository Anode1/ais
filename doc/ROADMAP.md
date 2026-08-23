# AIS: Roadmap

AIS is one small ANSI C engine (`c/`) with thin wrappers over a single FFI seam
(`embed.h`: `ais_embed_open` / `store` / `recall` / `timeline` / `tags` / …).
Almost everything below is a *wrapper* or a *packaging* task over that unchanged
engine, which is what keeps each piece tractable for one contributor at a time.
Help is welcome: open an issue to claim a piece.

## Shipped

- **Command line** (`ais`): Linux and macOS.
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
- **Native Windows app** (`win32/`, pure Win32 over the engine): built and
  CI-validated. Neither it nor the Windows CLI is published while the desktop GUI
  is reworked; what ships on each platform is in
  [`dev/DISTRIBUTION.md`](dev/DISTRIBUTION.md).

## Planned

Roughly in priority order. These are now all UI or platform glue over the
unchanged engine: the two former engine-level items — LAN sync and encrypted
secrets — have shipped (see above).

### iPhone (iOS) · next up

The **Android** app has shipped (above); **iOS** is the next focus. The same
**Flutter** app (`app/flutter/`) runs over the C engine through the FFI seam
(`embed.h` is the contract), and the iOS scaffold, its platform channels and the
`ais://` scheme are already written. The engine is wired in
(`ais_engine.podspec`), and CI builds the app unsigned on macOS and launches it
on a simulator, where the engine opens an index. What is left is the Apple side:
signing, a device, TestFlight. No interface work, since the screens are shared
with Android, and no core changes.
Issue [#1](https://github.com/Anode1/ais/issues/1) carries the whole brief: what
is missing, the signing steps and how to tell it works. A native Swift client
over the same `embed.h` seam is possible and nothing needs it; the spec that
described one is in git at `313fb42:doc/dev/IOS_NATIVE.md`.

A browser **PWA** (`app/`) is a parallel, lower-friction track (see
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

### Agent integration on Android

On the desktop an AI agent recalls from AIS by running the `ais` CLI as a tool,
spending far fewer tokens than re-searching its files (measured in *Compress the
Access*). On **Android** the same win needs a mobile seam: a way for an on-device
or connected agent to query the index — a share/intent entry point, or the FFI
`recall` exposed to a local agent runtime — so mobile agents get the same
near-zero-token recall the CLI gives today. Wrapper work over the unchanged
engine; `embed.h`'s `recall` is already the contract.

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

## Known gaps, as of v0.3.20

Four things are open, and this is the list to work from: the release chores that
remain, coverage nobody has, a test that cannot see, and defects left on purpose
with the reason for each.

### 1. Publishing v0.3.20 is not finished

The tag published ten artifacts. What still has to be done by hand:

- Upload `ais-v0.3.20-android.aab` to the Play Console and start the closed
  test, following [`dev/ANDROID_RELEASE.md`](dev/ANDROID_RELEASE.md). Take the
  screenshots from `screenshots/play/`, not the ones beside them: Play requires
  24-bit PNG with no alpha and refuses a side more than twice the other, and the
  plain captures fail both.
- Push the same `pkgver` to the AUR repository with `pkgrel=1` and a regenerated
  `.SRCINFO`. `packaging/aur/PKGBUILD` here is the reference copy, not the one
  users install.

### 2. Three things have never been verified anywhere

Not "tested and passing", but never run at all. Each needs hardware or time
rather than code:

- **A real arm64 phone on a real Wi-Fi network.** Every sync test to date went
  through emulator NAT (`10.0.2.2`) or `adb forward` over loopback. Untested:
  `.local`/mDNS names, a router with client isolation, and the arm64 and
  armeabi-v7a ABIs. This is the most likely source of a "sync does not work"
  report from a tester.
- **A multi-day soak.** Everything converges in seconds here. Nothing has tested
  a week of use, clock skew between two machines, or an index that grew.
- **Backgrounding mid-sync, and doze during the 120-second host wait.** The
  screen-awake flag is set on the host screen and the rest is unknown.

### 3. The desktop UI test cannot see

`tests/gui/flutter-sync.sh` drives the real Host/Join UI, and it is the only UI
coverage the Linux desktop build has. It SKIPs on a machine without the GTK
toolchain and, in CI, it builds and launches the app but every captured frame is
solid black, so it clicks blind and asserts nothing. `libgl1-mesa-dri`,
`libegl1`, `libgles2`, `LIBGL_ALWAYS_SOFTWARE=1`, `GALLIUM_DRIVER=llvmpipe` and a
24-bit Xvfb screen are all in place, so the cause is Flutter's GTK embedder on a
headless runner and it is unsolved. Until someone fixes it the drive step
reports instead of gating (see the note in `.github/workflows/flutter.yml`), and
the Android layers carry the real UI coverage. Fixing it would be worth it: the
desktop harness needs no device and runs in seconds.

### 4. One defect is knowingly unfixed

It is understood, loses no data, and is left for a stated reason.

- **A blob clash from before v0.3.20 leaves one duplicate record per device.**
  Names are unique at birth now, so this cannot happen to a new index, and a
  clash already on disk settles the moment every device updates. What no wire
  verb can do is retract the duplicates already minted. A user-invoked cleanup
  is possible; an automatic one is not, because two records pointing at
  identical bytes are two notes by design, not one duplicated note.

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
