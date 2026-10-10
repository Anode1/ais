# AIS: Roadmap

AIS is one small C99 engine (`c/`) with thin wrappers over a single FFI seam
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
  `aisc:` value; single-file XChaCha20-Poly1305 via monocypher in `c/crypto/`). Recall
  decrypts interactively; secrets are never emitted in plaintext by `--dump`.
- **Built-in LAN sync** (`c/sync.c`): one-way encrypted transfer (`--export --serve` /
  `--import <url> --token`) and two-way device sync (`--sync --serve` / `--sync <url>
  --token`) that converge in one round, end-to-end encrypted (XChaCha20-Poly1305 under a
  one-time token), LAN-only. See [`doc/SYNC.md`](SYNC.md).
- **Speak to recall**: the mic in the app's search field, Android and iOS,
  on-device recognition only ([dev/SPEECH.md](dev/SPEECH.md)).
- **Multiple named indexes** (`--switch` / `--indexes` / `--forget`) with a default
  project (`--project`).
- **Windows**: the CLI with the web GUI and LAN sync (Winsock), plus the native
  window (`win32/`, pure Win32 over the engine, search and add only), ship in
  one zip per release, cross-compiled from Linux; what ships on each platform
  is in [`dev/DISTRIBUTION.md`](dev/DISTRIBUTION.md).

## Planned

Roughly in priority order. These are now all UI or platform glue over the
unchanged engine: the two former engine-level items, LAN sync and encrypted
secrets, have shipped (see above).

### iPhone (iOS) · next up

The **Android** app has shipped (above); **iOS** is the next focus. The same
**Flutter** app (`app/flutter/`) runs over the C engine through the FFI seam
(`embed.h` is the contract), and the iOS scaffold, its platform channels and the
`ais://` scheme are already written. The engine is wired in
(`ais_engine.podspec`), and CI builds the app unsigned on macOS and launches it
on a simulator, where the engine opens an index. A tag builds, signs and
uploads the app to TestFlight, where it is installed for internal testers and
has synced with an Android phone (Known gaps 2). What is left is the App Store
submission and the device checks in
[`dev/IOS_TODO.md`](dev/IOS_TODO.md) section 8. No interface work, since the
screens are shared with Android, and no core changes.
Issue [#1](https://github.com/Anode1/ais/issues/1) carries the brief: what is
missing and how to tell it works. [`dev/IOS_RELEASE.md`](dev/IOS_RELEASE.md) is
the Apple side step by step, from a machine with no Mac. A native Swift client
over the same `embed.h` seam is possible and nothing needs it; the spec that
described one is in git at `313fb42:doc/dev/IOS_NATIVE.md`.

A browser **PWA** (`app/`) is a parallel, lower-friction track (see
[`dev/DISTRIBUTION.md`](dev/DISTRIBUTION.md) for the WASM/standalone plan).

### F-Droid (Android)

Publish the Android build on **F-Droid**, the free/open app store: a reproducible
build from source, no proprietary dependencies, plus the F-Droid metadata recipe.
Depends on the Android app above. Google Play is a separate, optional track.

### Speech support

**Speak to recall** has shipped (above); **speak to file** (PUT) is next.
On-device recognition where the platform provides it (iOS and Android
native speech APIs, not browser Safari, which is one reason iOS needs a native
shell). This is the seam toward the longer-horizon hands-free / wearable use.
Design and build order: [dev/SPEECH.md](dev/SPEECH.md).

### Agent integration on Android

On the desktop an AI agent recalls from AIS through `ais --mcp`, or by running
the `ais` CLI as a tool, spending far fewer tokens than re-searching its files
(measured in *Compress the Access*). On **Android** the same win needs a mobile
seam: a way for an on-device or connected agent to query the index (a
share/intent entry point, or the FFI `recall` exposed to a local agent runtime)
so mobile agents get the same near-zero-token recall the CLI gives today.
Wrapper work over the unchanged engine; `embed.h`'s `recall` is already the
contract.

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
SHA-256, or just build from source, which is never quarantined. **Windows
code-signing** is planned through the SignPath OSS program ([`dev/WINDOWS.md`](dev/WINDOWS.md)); the release workflow has no
signing step. SignPath's Foundation program declined the project in June 2026
as too new: it gates on community-adoption signals (stars, forks, third-party
references) that a fresh repo cannot yet show. Paid signing is not planned.
Reapply once the project has visible adoption. Until a build is signed, verify a
download by its SHA-256 or build from source (see the README).

### Sync through any storage

`--sync-folder` over any store two devices can both read and write: a paste
service, an object bucket, WebDAV, a public cache. The folder protocol already
fits it: each device writes only its own bundle and reads the others', nothing
has to be online at once, and the merge is order-independent. What is new is a
small adapter (list, get, put) under the existing folder pass, and a pairing
code that carries the store's address and a key, as `ais://sync` carries host
and token today.

Two rules decided up front. **On storage others can read, bundles are
encrypted**, always: the XChaCha20-Poly1305 transport the LAN sync already
uses, keyed from the pairing token or a passphrase. Plain bundles stay an
option only for storage the user alone can read. **An expired bundle is a
refusal**: a cache that drops a device's bundle is reported the way
`--sync-folder` reports a folder with no bundles, so it never reads as a fresh
start. The store is one the user picks; AIS runs no service of its own (see
*Not planned*).

### Open a bundle from an attachment

An `.aisb` file already travels by email (Sync > Export to a file opens the
share sheet; [`SYNC.md`](SYNC.md), "By email, or any app that carries a file").
On Android, tapping the attachment does not offer AIS, because the manifest
registers no file intent, so the user saves it to Downloads and imports from
there. An intent filter for `.aisb` (VIEW and SEND) that routes to the same
merge, behind the confirmation the `ais://sync` link already shows, removes
that step.

## Known gaps, as of v0.3.36

Four things are open, and this is the list to work from: the release chores that
remain, coverage nobody has, a test that cannot see, and defects left on purpose
with the reason for each.

### 1. Publishing is not finished

A tag publishes eight artifacts, each with a checksum, and uploads a signed
iOS build to TestFlight. Reaching the stores is manual:

- Upload `ais-v0.3.36-android.aab` to the Play Console as a new release on the
  Production track, following [`dev/ANDROID_RELEASE.md`](dev/ANDROID_RELEASE.md).
  The listing is done and a production release with build 477 (0.3.27) went
  to review on 2026-09-15, so every later build is another upload by hand. The
  listing text and graphics are `fastlane/metadata/android/en-US/`
  (`doc/public-text.txt` says what goes where).
- The iOS build is in App Store review (submitted 2026-10-09, build 556,
  [`dev/IOS_TODO.md`](dev/IOS_TODO.md) section 9); release it by hand after
  approval, then put the App Store link in the README.
- Publish the release to the official MCP Registry
  ([`dev/VERSIONING.md`](dev/VERSIONING.md), step 5).
- The AUR package does not exist yet. The first step is claiming the name;
  `packaging/aur/PKGBUILD` here is the reference copy for it.

### 2. Five things are barely verified

One has been run twice, by hand; the other four have never been run at all.
Each needs hardware or time rather than code:

- **A real arm64 phone on a real Wi-Fi network.** One pass exists: on 2026-08-24
  an Android phone on build 425 paired by QR with `ais --serve` on a laptop over
  ordinary Wi-Fi and converged both ways, and an earlier attempt that evening
  failed silently, which is what the 300-second host wait and the reported join
  result in v0.3.23 come from. The second pass, and the first with iOS: on
  2026-10-09 an iPhone on 0.3.34 synced with an Android phone on 0.3.29 over
  Wi-Fi. Every other sync test went through emulator NAT
  (`10.0.2.2`) or `adb forward` over loopback. Still untested: `.local`/mDNS
  names, a router with client isolation, and the armeabi-v7a ABI.
- **The iPhone checks in [`dev/IOS_TODO.md`](dev/IOS_TODO.md) section 8**:
  a cold-start scan, the second join after the local-network alert, and the
  2026-10-09 speech fixes (one phrase per session, the Search tab brought
  forward).
- **A multi-day soak.** Everything converges in seconds here. Nothing has tested
  a week of use, clock skew between two machines, or an index that grew.
- **Backgrounding mid-sync, and doze during the 300-second host wait.** The
  screen-awake flag is set on the host screen and the rest is unknown.
- **The native Windows window.** `native-windows.yml` runs the CLI and sync
  suites against `ais.exe` on a Windows runner; the window only
  cross-compiles there. The release zip was run by hand on a Windows PC on
  2026-10-09: `ais-web.bat` opened the web GUI and a sync with an iPhone
  converged. Neither developer has a Windows machine, so the window stays
  unrun by a person.

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

### 4. Four limits are knowingly left

Each is understood, loses no data, and is left for a stated reason.

- **A capital letter outside ASCII makes a different key.** `key_encode`
  lowercases ASCII only, so `Рецепт` and `рецепт` are two tags, and voice input
  capitalises the first word. The English user never meets this. The fix is a
  fixed fold table (Latin-1, Latin Extended-A, Greek, Cyrillic) in `key.c`,
  the same table in `find.c`, and recall reading a posting under both names
  when they differ, since an index written before the change files `Рецепт`
  under `idx/Р/`. The store is untouched, the format version stays, an older
  binary keeps working on the same folder, and `--compact` re-files the old
  names. Left until the iOS release is out. The UI itself stays English; a
  translation is a separate piece of work, wanted only when a request or the
  install figures name a country.

- **An edit reaches a device that predates v0.3.21 as a second record.** The
  `E|` verb that carries an in-place edit is skipped by an older build, which
  keeps the old text beside the new until it is updated. Updating every device
  is the fix; the stream rule in [`dev/MERGE.md`](dev/MERGE.md) is why the verb
  could not be written earlier.

- **Nothing reclaims the edit log.** The cost is in `limitations.txt`. The
  facts are what let an arbitrarily stale peer or backup jump to the current
  text in one round; a reclaim path needs a bound on how stale a peer can be,
  which nothing provides yet.

- **A blob clash from before v0.3.20 left one duplicate record per device, and
  only the user can retract them.** Names are unique at birth now, so this
  cannot happen to a new index. The duplicates already minted are for
  `ais --dedupe-docs`, run on each device with a sync between: it shows the
  copies beside the record kept and deletes what is confirmed. Automatic it
  cannot be, because by design two records pointing at identical bytes are two
  notes.

## Not planned (non-goals)

- **A .NET / WinUI wrapper.** The native Win32 app (`win32/`) already covers
  Windows with no runtime dependency, and .NET's framework churn works against
  the "tiny, dependency-free, built to outlive its own tools" goal. Win32 is a
  decades-stable API; a self-contained .NET build drags a large runtime for no
  capability a user can feel.
- **A heavyweight backend** (SQLite, a database, a server daemon). Plain text is
  the durability and transparency guarantee; see the README "Questions."
- **A cloud account or sync service.** Sync is peer-to-peer over your own files
  (the built-in LAN sync under *Shipped*, or Syncthing; see [`doc/SYNC.md`](SYNC.md));
  nothing phones home, by design.

## How to contribute

- **Keep the core pure.** C99 lives in `c/`; platform code and any
  C++/Swift/Dart stays isolated in its own wrapper directory (`win32/`, `app/`,
  a future `macos/`).
- **Build the engine as a library:** `make -C c lib`.
- **The contract is `embed.h`** (and the CLI). Wrappers call it; they never reach
  into the on-disk store format.
- Open an issue describing the wrapper or platform you want to take.

See [`dev/DISTRIBUTION.md`](dev/DISTRIBUTION.md) for the packaging plan and
[`dev/LAYOUT.md`](dev/LAYOUT.md) for the on-disk format and module map.
