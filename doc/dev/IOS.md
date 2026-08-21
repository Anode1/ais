# AIS on iOS

The AIS app ships on Android from `app/flutter/`. The Dart side, the iOS
scaffold, the platform channels and the `ais://` URL scheme are written. What is
missing is the C engine compiled into the iOS binary, plus signing and
distribution. This file covers that.

## The stack

AIS is a C99 engine (`c/`) with thin front ends over one FFI seam
(`c/embed.h`). The phone app is Flutter (Dart, `app/flutter/lib/`), one codebase
for Android and iOS. The CLI, the web GUI and the Win32 app are the other front
ends. All of them share one on-disk format and one wire format, so any front end
built on the engine syncs with the others.

Flutter renders its own widgets to a single surface, so the iOS screens are the
Android screens; UIKit and SwiftUI are not involved, and no Dart, Swift or
Objective-C source changes. The tree contains no Objective-C; the two
native files under `app/flutter/ios/Runner/` are Swift and are already written.
The work is Xcode target configuration, code signing, and the App Store Connect
pipeline.

The index is plain text in the app's Documents directory, the same format as
every other platform. There is no server, no account and no third-party SDK.

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

## Related documents

| Read | For |
| --- | --- |
| `app/flutter/README.md` | how the Flutter app is assembled |
| `doc/dev/VERSIONING.md` | where release version numbers come from |
| `doc/dev/LAYOUT.md` | the on-disk format, and what a delete disposes of |
| `doc/dev/SYNC_PROTOCOL.md`, `doc/dev/MERGE.md` | the wire protocol and the merge rules |
| `README.md`, `doc/about.txt` | what the product does |
