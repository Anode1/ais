# AIS on iOS

This file covers two separate pieces of work over the same engine.

**Part 1: the existing Flutter app on iOS.** The app ships on Android from
`app/flutter/`. The Dart side, the iOS scaffold, the platform channels and the
`ais://` URL scheme are written. What is missing is the C engine compiled into
the iOS binary, plus signing and distribution.

**Part 2: a native Swift client.** A separate interface written in SwiftUI over
the same C ABI. Not required for the app to ship on iPhone.

## The stack

AIS is a C99 engine (`c/`) with thin front ends over one FFI seam
(`c/embed.h`). The phone app is Flutter (Dart, `app/flutter/lib/`), one codebase
for Android and iOS. The CLI, the web GUI and the Win32 app are the other front
ends. All of them share one on-disk format and one wire format, so any front end
built on the engine syncs with the others.

Flutter renders its own widgets to a single surface, so in Part 1 the iOS screens
are the Android screens; UIKit and SwiftUI are not involved. Part 1 changes no
Dart, Swift or Objective-C source. The tree contains no Objective-C; the two
native files under `app/flutter/ios/Runner/` are Swift and are already written.
The work is Xcode target configuration, code signing, and the App Store Connect
pipeline.

The index is plain text in the app's Documents directory, the same format as
every other platform. There is no server, no account and no third-party SDK.

# Part 1: the Flutter app on iOS

## Requirements

| What | Notes |
| --- | --- |
| macOS with Xcode 15+ and CocoaPods | Required for every step below. |
| Flutter SDK `3.44.1` | Pinned to the version CI builds and tests with (`.github/workflows/`). `flutter doctor` reports the iOS row clean before anything else works. |
| An iPhone | The Simulator covers everything except the camera QR scan, speech recognition, and LAN sync against a real network. |
| Apple Developer Program membership | Required for TestFlight and the App Store. A free Apple ID covers development builds on a personal device. |
| A second device on the same Wi-Fi | For sync testing: an Android phone running AIS, or a machine running the CLI (`make` at the repo root builds it). |

## What the repository already contains

`app/flutter/ios/` is the Flutter scaffold, generated with `flutter create --org
com.aisindex --project-name ais --platforms=ios .`, bundle id
`com.aisindex.ais`, matching Android. Its native glue is implemented:

- `Runner/SceneDelegate.swift` holds the `ais/deeplink` channel, which delivers
  an `ais://sync?…` pairing link both live and at cold start.
- `Runner/AppDelegate.swift` holds the `ais/backup` channel, which sets
  `NSURLIsExcludedFromBackupKey` on the index directory (iOS backs up Documents
  to iCloud by default), and `ais/screen`, which holds the screen awake while a
  pairing QR is displayed.
- `Runner/Info.plist` registers the `ais` URL scheme and carries the microphone
  and speech-recognition usage strings. Voice search runs on Apple Speech through
  the `speech_to_text` plugin, the same one Android uses.

`lib/main.dart` branches on `Platform.isIOS` where behavior differs.

## The engine inside the Runner target

**The engine is statically linked on iOS.** `lib/ais_ffi.dart` loads it with
`DynamicLibrary.process()` rather than from a `.so` or `.dylib`, so the C symbols
have to be inside the app binary. Nothing ships alongside it.

The Runner target (`open ios/Runner.xcworkspace`) therefore needs:

- every `c/*.c` and `c/crypto/*.c` except `main.c` in *Compile Sources*, since a
  library has no `main`,
- `$(SRCROOT)/../../../c` on *Header Search Paths*,
- the C language dialect set to `c99`,
- default symbol visibility, which is what lets `DynamicLibrary.process()`
  resolve the symbols.

A podspec listing the same sources and header path, referenced from
`ios/Podfile`, is equivalent.

The file list has one source of truth: `app/flutter/src/CMakeLists.txt`, the
build Android and Linux use. A second list maintained by hand drifts from it.

`c/crypto/` (`ais_crypto.c` and the vendored `monocypher.c`) is not optional.
Omitted, the app builds and launches, then fails with an undefined `aisc_*`
symbol the first time a secret is encrypted or revealed.

## Running it

    cd app/flutter && flutter pub get
    flutter run -d <simulator-id>     # no Apple account required
    flutter run -d <device-id>        # requires signing, below

Encrypt followed by Reveal is the path that proves the crypto sources linked, so
it is the useful first check on the Simulator, alongside save and recall.

## Signing and distribution

**Personal device, free Apple ID.** An Apple ID added under Xcode → Settings →
Accounts provides a personal team. With *Automatically manage signing* enabled in
the Runner target's *Signing & Capabilities*, Xcode registers the device and
issues a development profile. Builds expire after 7 days and the number of app
IDs is limited, so this covers development rather than distribution.

**TestFlight.** Requires the paid Developer Program. The app record is created in
App Store Connect under the same bundle id, and the build reaches it through
Product → Archive in Xcode or an uploaded `.ipa`. Testers are invited by email
and register no devices. Builds expire after 90 days.

**App Store.** The same upload, plus the review submission: screenshots,
description, privacy questionnaire.

**Export compliance.** The app contains non-Apple cryptography (vendored
Monocypher, XChaCha20-Poly1305), used to encrypt secrets on the device and to
encrypt LAN sync. The App Store Connect questionnaire covers this, and an
unanswered question blocks the build from distribution.

## Release builds

    flutter build ipa --release $(sh tool/version.sh)

The flags derive the version from the git tag. A flagless build stamps the
fallback version in `pubspec.yaml`, which `doc/dev/VERSIONING.md` treats as
unusable. The bundle id is `com.aisindex.ais`.

## Verification

The build is working when, on a physical iPhone:

- a record saved with tags recalls by tag, appears under Recent, survives a tag
  edit, and a delete of it can be undone;
- a secret encrypted with a passphrase reveals with that passphrase;
- a multi-line note is stored as a document file and reads back in full;
- **sync converges in both directions.** With `./ais --sync --serve` running on
  the other machine, the phone's Sync & backup → Join, given the printed address
  and token, crosses records both ways; hosting from the phone and joining from
  the other machine with `./ais --sync http://… --token …` does the same;
- an `ais://sync?host=…&token=…` link opened from Safari or the camera brings up
  the app with Join prefilled, and asks for confirmation before syncing;
- voice search prompts for permission once, then transcribes;
- records persist across a force-quit, and the console logs no iCloud-exclusion
  warning.

## Failure modes

- An undefined `aisc_*` symbol at runtime, on the first encrypt or reveal, means
  a `c/crypto/*.c` file is missing from the target. The no-undefined-crypto
  assertion in `app/flutter/src/CMakeLists.txt` names the required files.
- An engine built as a separate framework rather than compiled into Runner has to
  be linked into the final binary. `DynamicLibrary.process()` resolves nothing
  from an unlinked framework.
- Same engine sources, same `embed.h` seam, index in the documents directory. An
  iOS-only variation breaks sync with the user's other devices.

Optional afterwards: Siri Shortcuts, so "Hey Siri, ask AIS to recall …" reaches
`recall()` through the same seam.

# Part 2: a native Swift client

The index, the crypto, the sync protocol and the merge stay in the C engine
behind `c/embed.h`. This part is a SwiftUI interface plus glue over roughly
twenty C functions.

## The ABI (`c/embed.h`)

Handles are opaque `void *`, one per open index. Returned strings are
heap-allocated C strings, released with `ais_embed_free`.

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
                (ais_embed_pull / ais_embed_serve are the one-way variants; the
                 sync_* pair backs a single "Sync" control)

Sync return codes: `0` merged, `-1` bad arguments or malformed URL, `-2` no peer
completed (timeout, wrong token, connect failure), `-3` port busy (bind failed,
returned immediately rather than after the timeout). None of these functions
print.

`ais_embed_open` registers the payload disposer itself. Code calling `ais_open`
directly needs `ais_on_discard` after it, or a document deleted on another device
leaves its file behind permanently. `doc/dev/LAYOUT.md` states what a delete
disposes of.

## Constraints not visible in the signatures

- **Nothing engine-side belongs on the main thread.** `sync_serve` blocks up to
  ~120 s waiting for a peer, and `sync_pull` blocks for the transfer, so both
  belong on a background `Task` or `DispatchQueue`. The Flutter app uses a
  background isolate.
- **One caller per handle.** The handle is single-writer, and a `recall` during a
  `sync` on the same handle is a data race, so engine calls serialize onto one
  queue.
- **One sync at a time.** A scanned deep link can arrive while a sync is running.
  The Flutter app keeps a `_syncBusy` flag and refuses the second call.
- `ais_embed_open` holds the single-writer lock for the handle's lifetime, so one
  long-lived handle per index is the pattern, and the same directory is never
  opened twice.
- SIGPIPE from a dropped socket is ignored inside the embed layer, so a peer
  hanging up mid-sync does not terminate the process. There is no `fork()` on the
  embed path (that is only in the CLI and the web host), so it is iOS-safe.

## Sync and pairing

`sync_pull` joins, `sync_serve` hosts. Both sides converge in one round because
the merge is a CRDT (`doc/dev/MERGE.md`), so there is no fixed sender or
receiver. Wire protocol and security model: `doc/dev/SYNC_PROTOCOL.md`.

- **Token.** The host generates a 128-bit random hex token
  (`SecRandomCopyBytes`), displays it, and passes it to `sync_serve`; the joiner
  passes the same value. The token does not cross the wire, since the engine
  performs challenge-response, and a wrong token is rejected before anything
  merges. A wrong token does not end the host's session, so a mistyped one can be
  re-entered.
- **Pairing link**, the contract shared with the Android app and the web GUI:

      ais://sync?host=<percent-encoded ip:port>&token=<hex>

  `host` is `ip:port` percent-encoded (`:` becomes `%3A`). Joining from a scanned
  link means decoding `host`, building `url = "http://" + host`, and calling
  `sync_pull(url, token)`. Hosting means resolving this device's LAN IPv4
  (preferring a private range), picking a port (the app uses 8766), and rendering
  the link as a QR code.
- **A scanned link needs confirmation before it is joined.** It can originate
  anywhere, and a sync shares this device's records, so the peer is named and the
  user confirms first.

## iOS platform integration

- **Info.plist** carries the reusable entries (see
  `app/flutter/ios/Runner/Info.plist`): `NSLocalNetworkUsageDescription`, since
  the Local Network prompt fires on the first bind, and `CFBundleURLTypes`
  registering the `ais` scheme.
- **Deep links.** The `ais://` scheme routes to the scene under the UIScene
  lifecycle, through `SceneDelegate.scene(_:openURLContexts:)`.
  `app/flutter/ios/Runner/SceneDelegate.swift` is a working reference for the
  routing; it forwards the URL, where a native client parses and joins directly.
- **QR.** Rendering takes any drawing code. Scanning runs on Vision
  (`VNDetectBarcodesRequest`) or AVFoundation metadata output, with no
  third-party dependency, so in-app scanning can sit alongside the camera-app
  deep-link path.
- **Index location.** The app's Documents directory, or an App Group container to
  share the index with an extension, passed to `ais_embed_open(dir)`.

## Building libais for iOS

`make lib` produces `libais.a`: the engine without `main`, with sync and the
vendored Monocypher compiled in and no external dependency. For iOS that means
one slice per SDK and architecture, assembled into an xcframework. The script
below runs on macOS with Xcode and is worth verifying on first use.

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

The resulting `libais.xcframework` goes into the app target and `embed.h` into
the bridging header, after which the C functions are callable from Swift.

## The Flutter app as a behavioral reference

Not a dependency. Where the non-obvious behavior is already resolved:

    app/flutter/lib/main.dart          _genToken (128-bit hex), _lanIp (prefer private range),
                                       _handleLink (parse + confirm), _syncBusy (one at a time),
                                       background-isolate calls, the barrier dialog while a sync blocks
    app/flutter/lib/ais_ffi.dart       how the FFI seam marshals the same embed.h calls
    app/flutter/ios/Runner/            Info.plist (scheme + local network), SceneDelegate (deep link)

These carry the exact success and failure messaging and the concurrency
discipline.

## Related documents

| Read | For |
| --- | --- |
| `app/flutter/README.md` | how the Flutter app is assembled |
| `doc/dev/VERSIONING.md` | where release version numbers come from |
| `doc/dev/LAYOUT.md` | the on-disk format, and what a delete disposes of |
| `doc/dev/SYNC_PROTOCOL.md`, `doc/dev/MERGE.md` | the wire protocol and the merge rules |
| `README.md`, `doc/about.txt` | what the product does |
