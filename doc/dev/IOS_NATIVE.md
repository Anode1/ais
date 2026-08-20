# AIS on iOS

Everything an iOS developer needs, in one file. There are two possible jobs here
and they are very different sizes, so start by reading which one you have.

**Job one: ship the Flutter app on iPhone.** The app already exists and ships on
Android from the same code. The screens, the sync, the encryption and the deep
links are written and already iOS-aware. What is missing is compiling the C
engine into the iOS binary, plus signing and the store. About a day of Xcode
work, then Apple's own timescale. Part 1.

**Job two: a native Swift client.** A separate, much larger project: the whole
interface written again in SwiftUI over the same C engine. Nobody needs it to
ship on iPhone, and doing job one first is how you learn the engine and the
Apple pipeline before taking it on. Part 2.

## Orientation

AIS is a small C99 engine (`c/`) with thin front ends over one FFI seam
(`c/embed.h`). The phone app is **Flutter** (Dart, in `app/flutter/lib/`), one
codebase for Android and iOS. The CLI, the web GUI and the Win32 app are the
other front ends. They share one on-disk format and one wire format, so anything
built on the engine syncs with everything else for free.

For job one you write no Objective-C (there is none in the tree), no SwiftUI or
UIKit, and no Dart. Flutter draws its own interface onto a single surface, so
the iPhone screens are the Android screens. What the job actually needs is Xcode
target and build-setting work, code signing, provisioning, archiving and App
Store Connect, which is the half a Flutter developer usually finds hardest.

The index lives in the app's Documents directory as plain text, the same format
as every other platform. There is no server, no account, no third-party SDK.

# Part 1: ship the Flutter app on iPhone

## What you need

| What | Notes |
| --- | --- |
| A Mac with Xcode 15+ and CocoaPods | The whole job happens here. |
| Flutter SDK, version `3.44.1` | Pinned: it is what CI builds and tests with (`.github/workflows/`). Run `flutter doctor` until the iOS row is green. |
| An iPhone | The Simulator covers everything except the camera QR scan, real speech, and true LAN sync. |
| Apple Developer Program, $99/yr | For TestFlight and the App Store. A free Apple ID is enough to run on your own device while developing. |
| A second device on the same Wi-Fi | To test sync: an Android phone with AIS, or a Mac running the CLI (`make` at the repo root builds it). |

## What already exists

`app/flutter/ios/` is the Flutter scaffold (generated with `flutter create --org
com.aisindex --project-name ais --platforms=ios .`, bundle id `com.aisindex.ais`
to match Android), and the native glue in it is written and working:

- `Runner/SceneDelegate.swift`: the `ais/deeplink` channel, delivering an
  `ais://sync?…` pairing link live and at cold start.
- `Runner/AppDelegate.swift`: the `ais/backup` channel, which sets
  `NSURLIsExcludedFromBackupKey` on the index directory (iOS backs Documents up
  to iCloud by default), and `ais/screen`, which keeps the screen awake while a
  pairing QR is displayed.
- `Runner/Info.plist`: the `ais` URL scheme, plus the microphone and speech
  usage strings. Voice search uses Apple Speech through the same Flutter plugin
  Android uses.

The Dart side branches on `Platform.isIOS` where it has to. So the app, not just
the engine, is already iOS-aware.

## The one real task: the engine into the Runner target

**On iOS the engine is statically linked.** `lib/ais_ffi.dart` loads it with
`DynamicLibrary.process()`, not from a `.so` or `.dylib`, so the C symbols must
end up inside the app binary. There is no library to ship alongside.

In Xcode (`open ios/Runner.xcworkspace`):

- add every `c/*.c` **and** `c/crypto/*.c` except `main.c` to the Runner target's
  *Compile Sources* (a library has no `main`),
- set *Header Search Paths* to include `$(SRCROOT)/../../../c`,
- set the C language dialect to `c99`,
- leave symbol visibility at the default, so `DynamicLibrary.process()` resolves
  them.

A small podspec listing the same sources and header path, referenced from
`ios/Podfile`, is equivalent. Either way the file list has one source of truth:
`app/flutter/src/CMakeLists.txt`, the build Android and Linux already use. Mirror
it exactly and never fork the engine for iOS.

`c/crypto/` (`ais_crypto.c` and the vendored `monocypher.c`) is not optional. Left
out, the app builds and launches, then dies with an undefined `aisc_*` symbol the
first time a secret is encrypted or revealed.

## Running it

    cd app/flutter && flutter pub get
    flutter run -d <simulator-id>     # no Apple account needed
    flutter run -d <device-id>        # after signing, below

Exercise saving, recall, and especially Encrypt then Reveal on the Simulator: that
is the path that proves the crypto sources linked.

## Getting it onto an iPhone

Three levels, in the order you will want them.

**Your own device, free.** Add a plain Apple ID under Xcode → Settings →
Accounts. Select the Runner target → *Signing & Capabilities*, tick *Automatically
manage signing*, choose that personal team. Xcode registers the device and issues
a development profile. The build expires after **7 days** and the number of app
IDs is limited: fine for development, no use for handing to anyone.

**Testers, through TestFlight.** Needs the paid Developer Program. Create the app
record in App Store Connect under the same bundle id, then Product → Archive in
Xcode (or upload the `.ipa`) and invite testers by email. Builds last 90 days and
testers need no device registration.

**The App Store.** The same upload plus the review submission: screenshots,
description, privacy questionnaire.

**Export compliance has a real answer.** The app carries non-Apple cryptography
(vendored Monocypher, XChaCha20-Poly1305) used to encrypt the user's own secrets
on the device and to encrypt LAN sync. Answer the App Store Connect questionnaire
rather than skipping it; this use ordinarily qualifies for an exemption, but an
unanswered question stalls the build.

## Shipping a release

    flutter build ipa --release $(sh tool/version.sh)

The flags derive the version from the git tag. A flagless build stamps the stale
fallback in `pubspec.yaml`, which `doc/dev/VERSIONING.md` treats as unusable
rather than something to reconcile. Keep the bundle id `com.aisindex.ais`.

## Acceptance: how to know it works

On a real iPhone, all of these:

- Save a record with tags, find it by tag, see it under Recent, edit its tags,
  delete it and undo.
- Encrypt a secret with a passphrase, then Reveal it (the crypto-linking test).
- Save a multi-line note, which becomes a document file, and read it back whole.
- **Sync both ways.** On the Mac run `./ais --sync --serve`; on the phone open
  Sync & backup → Join and type the address and token it prints. Records must
  cross in both directions. Then Host on the phone and join from the Mac with
  `./ais --sync http://… --token …`.
- Open an `ais://sync?host=…&token=…` link from Safari or the camera: the app
  opens with Join prefilled and asks before syncing.
- Voice search asks permission once, then transcribes.
- Force-quit and relaunch: everything is still there, and the console shows no
  iCloud-exclusion warning.

## Gotchas

- Undefined `aisc_*` at runtime on the first encrypt or reveal means a
  `c/crypto/*.c` file is missing from the target (the no-undefined-crypto
  assertion in `app/flutter/src/CMakeLists.txt` names them).
- If you build the engine as a separate framework instead of compiling into
  Runner, make sure it is genuinely linked into the final binary. `process()`
  finds nothing in an unlinked framework.
- Same engine sources, same `embed.h` seam, index in the documents directory. An
  iOS-only special case breaks sync with the user's other devices.

Later, if wanted: Siri Shortcuts, so "Hey Siri, ask AIS to recall …" reaches
`recall()` (the seam a future glasses client would ride too).

# Part 2: a native Swift client

Only if a hand-written iOS interface is wanted. You still do not reimplement the
index, the crypto, the sync protocol or the merge: all of that is portable C
behind `c/embed.h`. You write the SwiftUI interface plus a thin layer of glue
over about twenty C functions.

## The ABI you call (`c/embed.h`)

Handles are opaque `void *`, one per open index. Returned strings are
heap-allocated C strings you release with `ais_embed_free`.

    lifecycle   ais_embed_open(dir) -> void*        open/create, takes the single-writer lock
                ais_embed_close(h)                  release lock, flush, free
                ais_embed_locate(out,outsz)         resolve the same index the CLI would use
                ais_embed_default_set(dir)          persist "change store"

    data        ais_embed_recall(h,keys,or_mode)    -> "id|value\n" lines (0=AND, 1=OR)
                ais_embed_store(h,keys,value)        -> id>0
                ais_embed_store_encrypted(h,k,v,pw)  -> id>0 (inline "aisc:" secret)
                ais_embed_reveal(marked,pw)          decrypt an inline "aisc:" value
                ais_embed_del(h,id) / _update(h,id,keys)
                ais_embed_timeline(h,before,count,from,to) -> "id|ts|keys|value\n" lines
                ais_embed_tags(h)                    -> "count|key\n" lines
                ais_embed_compact(h,forget)          drop deleted records, reclaim their space
                ais_embed_free(buf)                  free any returned string

    sync        ais_embed_sync_pull(h,url,token)     join: connect, exchange, both converge
                ais_embed_sync_serve(h,port,token)   host: wait for one peer, both converge
                (ais_embed_pull / ais_embed_serve are the one-way variants; prefer the
                 sync_* pair for a unified "Sync" button)

Return codes (sync): `0` merged, `-1` bad args or malformed URL, `-2` no peer
completed (timeout, wrong token, connect failure), `-3` port busy (bind failed,
returned at once rather than after the timeout). None of these functions print.

Register a disposer with `ais_on_discard` after opening if you call the engine
directly rather than through `ais_embed_open`, which already does it: without it
a document deleted on another device leaves its file behind here forever. See
`doc/dev/LAYOUT.md` on what a delete disposes of.

## The rules that are not in the signatures

- **Never call on the main thread.** `sync_serve` blocks up to ~120 s waiting for
  a peer; `sync_pull` blocks for the transfer. Run every sync call on a background
  `Task` or `DispatchQueue`. The Flutter app uses a background isolate for exactly
  this reason.
- **One caller per handle at a time.** The handle is single-writer; a `recall`
  during a `sync` on the same handle is a data race. Serialize engine calls onto
  one queue.
- **One sync at a time.** A scanned deep link can arrive while a sync is running.
  The Flutter app keeps a `_syncBusy` flag and refuses the second; do the same.
- `ais_embed_open` holds the single-writer lock for the handle's lifetime. Keep
  one long-lived handle per index and do not open the same directory twice.
- SIGPIPE from a dropped socket is already ignored inside the embed layer, so a
  peer hanging up mid-sync will not kill the process. There is no `fork()` on the
  embed path (that lives only in the CLI and the web host), so it is iOS-safe.

## Sync and pairing

Use the bidirectional pair: `sync_pull` to join, `sync_serve` to host. Both sides
converge in one round because the merge is a CRDT (`doc/dev/MERGE.md`), so there
is no fixed sender or receiver. Wire protocol and security model:
`doc/dev/SYNC_PROTOCOL.md`.

- **Token.** Generate a 128-bit random hex token with `SecRandomCopyBytes`, show
  it, and pass it to `sync_serve`; the joiner passes the same. It never crosses
  the wire (the engine does challenge-response), and a wrong token is rejected
  before anything merges. A wrong token does not end the host's session, so a
  typo can simply be retyped.
- **Pairing link**, the contract shared with the Android app and the web GUI:

      ais://sync?host=<percent-encoded ip:port>&token=<hex>

  `host` is `ip:port` percent-encoded (`:` becomes `%3A`). To join from a scanned
  link: decode `host`, build `url = "http://" + host`, call `sync_pull(url,
  token)`. To host: find this device's LAN IPv4 (prefer a private range), pick a
  port (the app uses 8766), render that link as the QR.
- **Confirm before joining a scanned link.** A link can come from anywhere and a
  sync shares this device's records, so name the peer and ask first.

## iOS platform glue

- **Info.plist** already carries the reusable pieces (see
  `app/flutter/ios/Runner/Info.plist`): `NSLocalNetworkUsageDescription`, since
  the Local Network prompt fires on the first bind, and `CFBundleURLTypes`
  registering the `ais` scheme.
- **Deep links.** The `ais://` scheme routes to the scene under the UIScene
  lifecycle; handle it in `SceneDelegate.scene(_:openURLContexts:)`.
  `app/flutter/ios/Runner/SceneDelegate.swift` is a working reference for the
  routing (it forwards the URL; a native app parses and joins directly).
- **QR.** Display with any renderer. Scan natively with Vision
  (`VNDetectBarcodesRequest`) or AVFoundation metadata output, with no
  third-party dependency, unlike Android's ML Kit. In-app scanning is cheap
  enough on iOS to offer alongside the camera-app deep-link path.
- **Index location.** The app's Documents directory, or an App Group container to
  share the index with an extension, then `ais_embed_open(dir)`.

## Building libais for iOS

The static-library target exists: `make lib` produces `libais.a`, the engine
minus `main`, with sync and the vendored Monocypher compiled in and no external
dependency. For iOS, build one slice per SDK and arch, then assemble an
xcframework. Run on macOS with Xcode; verify on first use.

    # build one static slice for a given SDK + arch into a slice dir
    # (usage: slice <sdk> <arch> <min-flag> <outdir>)
    slice() {
      SDK=$(xcrun --sdk "$1" --show-sdk-path)
      CC="$(xcrun -f clang) -isysroot $SDK -arch $2 $3"
      make -C c clean >/dev/null
      make -C c libais.a CC="$CC" AIS_STD="-std=c99"
      mkdir -p "$4" && cp c/libais.a "$4/"
    }

    # device (arm64) and simulator (arm64 + x86_64); merge the two sim arches
    slice iphoneos          arm64  -mios-version-min=13.0            build/ios-arm64
    slice iphonesimulator   arm64  -mios-simulator-version-min=13.0  build/sim-arm64
    slice iphonesimulator   x86_64 -mios-simulator-version-min=13.0  build/sim-x86_64
    lipo -create build/sim-arm64/libais.a build/sim-x86_64/libais.a -output build/sim/libais.a

    # wrap both platforms into one xcframework, carrying the public header
    xcodebuild -create-xcframework \
      -library build/ios-arm64/libais.a -headers c/embed.h \
      -library build/sim/libais.a       -headers c/embed.h \
      -output build/libais.xcframework

Then drag `libais.xcframework` into the app target, add `embed.h` to the bridging
header, and call the C functions from Swift.

## The Flutter app as a behavioral reference

Not a dependency, just where the non-obvious behavior is already worked out:

    app/flutter/lib/main.dart          _genToken (128-bit hex), _lanIp (prefer private range),
                                       _handleLink (parse + confirm), _syncBusy (one at a time),
                                       background-isolate calls, the barrier dialog while a sync blocks
    app/flutter/lib/ais_ffi.dart       how the FFI seam marshals the same embed.h calls
    app/flutter/ios/Runner/            Info.plist (scheme + local network), SceneDelegate (deep link)

Read those for the exact success and failure messaging and the concurrency
discipline.

## Where else to look

| Read | For |
| --- | --- |
| `app/flutter/README.md` | how the Flutter app is put together |
| `doc/dev/VERSIONING.md` | why the release build flags are not optional |
| `doc/dev/LAYOUT.md` | the on-disk format, and what a delete disposes of |
| `doc/dev/SYNC_PROTOCOL.md`, `doc/dev/MERGE.md` | the wire protocol and the merge rules |
| `README.md`, `doc/about.txt` | what the product is, which testing judgment needs |
