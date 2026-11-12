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
- **Windows**: the CLI with the web GUI and LAN sync (Winsock) and `ais-gui.exe`,
  a tray launcher for the web GUI, ship per release as a per-user installer
  ("AIS" in the Start menu and on the Desktop, `ais` on the PATH) and as a zip,
  cross-compiled from Linux; what ships on each platform is in
  [`dev/DISTRIBUTION.md`](dev/DISTRIBUTION.md). The native window that shipped
  up to v0.3.37 was removed: search and add only, one line of text,
  its own vocabulary, and the first thing a Windows user opened.

## Planned

Roughly in priority order.

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

### Scale: a save that does not read the store

Measured 2026-11-04 on Linux with a warm page cache, after the tag-completion
fix of v0.3.44 (a keystroke reads one shard: 3.3 ms for a one-letter prefix at
28.6k tags, 0.13 ms for two letters, independent of the record count):

| Operation | 10k records | 110k records | Grows with |
| --- | --- | --- | --- |
| save a new value | 3.0 ms | 22.6 ms | store size, about 2.3 ms per MB |
| open the Tags view | 13.6 ms | 111 ms | tag count, one file per tag |
| Recent, 20 rows | 0.9 ms | 1.1 ms | nothing |
| recall a tag of 7654 records | | 116 ms | result count |
| `--import` of 110k `KEY -v VALUE` lines | | 17 min | records squared |

An old phone pays roughly ten times these; that factor is unmeasured.

Three items, in order:

1. **Save without the store scan.** `ais_put_at_k` enforces "a value names one
   record" with `store_find_value`, a parse of every store line. At a million
   records that is a 90 MB read per save, about 200 ms here and seconds on an
   iPhone 11, paid again by every incoming record on sync and by every line of
   a `KEY -v VALUE` import (only the `A|` format spools its lookups; LAYOUT.md's
   "an import does not pay it" holds for that format alone). The rule stays;
   the lookup moves to an index: `hash/<p>/<content_hash>` holding the id,
   written beside the postings at put, rebuilt by `--compact`, disposable like
   `idx/`. A hit is confirmed by reading the store line back, the check
   `feed.c`'s batch already makes (FNV-1a is not a security hash).
   `store_find_value` stays as the fallback while the directory is absent, so an
   index written before the change works until its next compaction.

2. **The Tags view without one file per tag.** `ais_tags_page` counts every
   posting file on each call, and its collector stops at 65,536 distinct tags
   (`AIS_KEYS_LIST_MAX`), where the view fails. At 286k tags the scan is about
   a second here. A `cloud` file, one `count|key` line per tag sorted busiest
   first, rewritten by compaction and kept current by put, update and delete,
   makes the view one read. Until then the three Tags views load the cloud once
   per open, not per page.

3. **Spool the `KEY -v VALUE` import** as the `A|` path does, unless item 1
   makes the lookup cheap enough to leave it per line.

Why now: Marina's iPhone 11 stalled for seconds at a few thousand tags, a size
no test had run; the fixture under `tests/` is a few dozen records. A generated
index of 100k records as a timing layer (`tests/scale/`), asserting that save,
completion and Recent stay under fixed bounds, shows the next size-proportional
cost before a user meets it.

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

A native macOS wrapper over the engine, so Mac users get a real app, not only
the web GUI via a launcher. A minimal AppKit/Swift shell calling `embed.h`. The
Windows native window was removed for lagging the web GUI by half the product;
a macOS one earns its place only at parity.

### Signing and notarization

So a *downloaded* build runs without security warnings. **macOS notarization**
(Apple Developer ID: `codesign` + `notarytool` + `staple` in CI) would remove the
"Apple could not verify 'ais' is free of malware" Gatekeeper block on downloaded
binaries, but it requires the paid Apple Developer Program ($99/year) and is not
planned. Meanwhile, clear the quarantine flag once with
`xattr -dr com.apple.quarantine .` (see the README), verify a download by its
SHA-256, or build from source, which is never quarantined. **Windows
code-signing** is planned through the SignPath OSS program ([`dev/WINDOWS.md`](dev/WINDOWS.md)); the release workflow has no
signing step. SignPath's Foundation program declined the project in June 2026
as too new: it gates on community-adoption signals (stars, forks, third-party
references) that a fresh repo cannot yet show. Paid signing is not planned.
Reapply once the project has visible adoption.

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

## Known gaps, as of v0.3.47

Four things are open: the release chores that remain, what is barely verified, a
test that cannot see, and defects left on purpose with the reason for each.

### 1. Publishing is not finished

A tag publishes nine artifacts, each with a checksum, and uploads a signed
iOS build to TestFlight. Reaching the stores is manual:

- Each tag's `.aab` goes to the Play Console by hand as a new release on the
  Production track, following [`dev/ANDROID_RELEASE.md`](dev/ANDROID_RELEASE.md);
  0.3.38 went up on 2026-10-24 and is live; 0.3.39's went up on 2026-10-28;
  0.3.40 changed nothing in the app and was not uploaded; Production is build
  627 (0.3.43) since 2026-11-03; 0.3.47's (build 640) follows. The listing
  text and graphics are `fastlane/metadata/android/en-US/`
  (`doc/public-text.txt` says what goes where). The appendix of that file is the way to let CI do the upload.
- iOS build 556 (0.3.36) is in App Store review, resubmitted 2026-10-14 with
  the notes and recording Apple asked for ([`dev/IOS_TODO.md`](dev/IOS_TODO.md)
  section 9); release it by hand after approval, then put the App Store link
  in the README. The v0.3.47 tag puts build 640 on TestFlight; it goes to
  review as an update once 556 is through.
- The official MCP Registry: 0.3.37 was published by hand on 2026-10-15;
  from v0.3.38 the release job publishes each tag's entry itself, and
  [`dev/VERSIONING.md`](dev/VERSIONING.md) step 5 has the fallback.
- winget: the first manifest (0.3.36, the release zip as a portable package)
  is microsoft/winget-pkgs pull request 449881, opened 2026-10-09; the
  pipeline passed the same night (Azure-Pipeline-Passed,
  Validation-Completed) and it waits for a moderator's merge. Measured on
  2026-10-13 over the last two weeks of merges, a New-Package pull request
  merges 29 days after opening at the median, 39 at the 90th percentile, so
  expect 5-17 November 2026. After the merge: a `winget install Anode1.AIS`
  line in the README's Download section, and every later tag needs a new
  version directory under `installer/winget/` and an update pull request
  (its README).
- The AUR package does not exist yet. The name `ais` was free on 2026-10-13
  (only `ais-catcher`, a marine receiver, is near it), and that day the AUR
  had closed new-account registration against automated sign-ups, with no
  date given; aur-general announces the reopening. `packaging/aur/PKGBUILD`
  here is the reference copy for it.
- The openSUSE Build Service project, which gives Debian, Ubuntu, Fedora and
  openSUSE users a repository, is not created yet. `packaging/obs/README.md`
  is the procedure; its spec and Debian source built and passed both test
  layers on Fedora 44 and Debian 12 in Docker on 2026-10-13. The account
  `Anode1` exists in the SUSE identity portal since 2026-10-14 and accepts
  its password there, while build.opensuse.org and the API answered 401 to
  the same pair on 2026-10-14 and 2026-10-15; admin@opensuse.org was mailed
  on 2026-10-15. `osc api /person/Anode1` is the check.

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
  names, a router with client isolation, and a real armeabi-v7a phone. Build
  627 crashed on that ABI (Play reported three SIGSEGV clusters, one user
  each) because the Dart binding declared a C `long` as 64 bits.
  `tests/ffi-abi.sh` refuses that declaration, `tests/abi32/` runs the engine
  in a 32-bit container, and `tests/gui/flutter-crud-android32.sh` runs the
  armeabi-v7a app under an emulator's ARM translation, where the old binding
  dies at launch. Translation is not the hardware. The folder pass that drops itself
  when a sync took the handle during its wait (v0.3.47) has no test either:
  it needs the engine library and an Android storage tree in one widget test.
- **The iPhone checks in [`dev/IOS_TODO.md`](dev/IOS_TODO.md) section 8**:
  a cold-start scan, the second join after the local-network alert, and the
  2026-10-09 speech fixes (one phrase per session, the Search tab brought
  forward).
- **A multi-day soak.** Everything converges in seconds here. Nothing has tested
  a week of use, clock skew between two machines, or an index that grew.
- **Backgrounding mid-sync, and doze during the 300-second host wait.** The
  screen-awake flag is set on the host screen and the rest is unknown.
- **The Windows installer and launcher by a person.** `native-windows.yml`
  runs the CLI and sync suites against `ais.exe`, starts and quits
  `ais-gui.exe`, and installs and uninstalls the setup exe silently, all on a
  Windows runner. No person has yet run the installer wizard, clicked the
  Start-menu entry or the tray icon, or seen the icon at Desktop size. The
  earlier zip was run by hand on a Windows PC on 2026-10-09: its `.bat`
  launcher opened the web GUI and a sync with an iPhone converged. Neither
  developer has a Windows machine.

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

### 4. Eight limits are knowingly left

Each is understood, loses no data, and is left for a stated reason.

- **The web page's count and toast are silent to a screen reader.** `#count`
  and `#toast` in `c/serve.c` are plain spans, so "12 items" and "Deleted" are
  never announced; the Due group is a named region, but its count shares the
  fate. The fix is `role=status` on both spans and the same on the PWA, found
  during the Due design review of 2026-11-02 and left for a change of its own.

- **A locked note holds at most 6000 bytes (3000 Cyrillic letters) in the
  app and on the web page.** An inline secret seals into
  an 8 KB buffer (`AIS_SECRET_INLINE_MAX` in `c/secret.h`); every front-end
  refuses a longer one in the same sentence before anything is sent. The CLI's
  `--doc -e` has no such cap, since it writes the secret as an encrypted blob.
  Routing a long locked note from the GUIs through that blob path is the fix,
  and it waits for a second request. The web page also refuses a plain note
  over 64 KB, which the app files as a document.

- **A capital letter outside ASCII makes a different key.** `key_encode`
  lowercases ASCII only, so `Рецепт` and `рецепт` are two tags, and voice input
  capitalises the first word. The English user never meets this. Tag completions already match across
  the two (`key_fold_cp`, the fold table in `key.c`). The fix for storage is that
  table in `key_encode`,
  the same table in `find.c`, and recall reading a posting under both names
  when they differ, since an index written before the change files `Рецепт`
  under `idx/Р/`. The store is untouched, the format version stays, an older
  binary keeps working on the same folder, and `--compact` re-files the old
  names. Left until the iOS release is out. The UI itself stays English; a
  translation is a separate piece of work, wanted only when a request or the
  install figures name a country.

- **An ASCII letter followed by a cased non-ASCII letter shards by bytes.**
  `key_prefix` takes two bytes when the first character is ASCII, so `aé` and
  `aÉ` file under two shards, each named by half a UTF-8 sequence, and a tag
  completion typed one way does not see a key stored the other way. Rare: it
  needs a key that mixes scripts in its first two letters. The fix is a
  two-character prefix in `key_prefix`, which changes where such keys are filed,
  so it comes with `--compact` re-filing them and a note in LAYOUT.md.

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

- **A save reads the whole store, and the Tags view opens one file per tag.**
  22.6 ms and 111 ms at 110k records on a desktop, about ten times that on an
  old phone, both growing with the index. The plan and the numbers are "Scale"
  under Planned.

- **Import bookmarks refuses Pocket's zip; the user opens it and picks the
  `part_NNNNNN.csv` files inside.** Reading the zip in place means an inflate
  routine of a few hundred lines in the engine, for a download that cannot be
  made anew since Pocket's export window closed in October 2025. The pickers take
  several files at once, so a library split across parts is one import.

## Not planned (non-goals)

- **A .NET / WinUI wrapper.** The web GUI already covers Windows with no
  runtime dependency, and .NET's framework churn works against the "tiny,
  dependency-free, built to outlive its own tools" goal. A self-contained .NET
  build drags a large runtime for no capability a user can feel.
- **A heavyweight backend** (SQLite, a database, a server daemon). Plain text is
  the durability and transparency guarantee; see the README "Questions."
- **A cloud account or sync service.** Sync is peer-to-peer over your own files
  (the built-in LAN sync under *Shipped*, or Syncthing; see [`doc/SYNC.md`](SYNC.md));
  nothing phones home, by design.

## How to contribute

- **Keep the core pure.** C99 lives in `c/`; platform code and any
  C++/Swift/Dart stays isolated in its own wrapper directory (`app/`, a future
  `macos/`).
- **Build the engine as a library:** `make -C c lib`.
- **The contract is `embed.h`** (and the CLI). Wrappers call it; they never reach
  into the on-disk store format.
- Open an issue describing the wrapper or platform you want to take.

See [`dev/DISTRIBUTION.md`](dev/DISTRIBUTION.md) for the packaging plan and
[`dev/LAYOUT.md`](dev/LAYOUT.md) for the on-disk format and module map.
