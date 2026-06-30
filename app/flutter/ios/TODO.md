# iOS native client — TODO for the next developer

This `ios/` folder is the Flutter scaffold only (generated with
`flutter create --org com.aisindex --project-name ais --platforms=ios .`, bundle id
`com.aisindex.ais`, to match Android). The Dart side is already iOS-ready; what is
missing is the native engine build and signing, neither of which can be done without a
Mac. If you have a Mac + Xcode + an Apple Developer account, this is for you.

## What you need to know first

- **The engine is statically linked on iOS.** `lib/ais_ffi.dart` loads the C engine with
  `DynamicLibrary.process()` (not a `.so`/`.dylib` file), so the engine's symbols must end
  up **inside the app binary**. There is no separate library to ship; you compile the C
  into the Runner target.
- **The FFI seam is `../../../c/embed.h`** (`ais_embed_open / recall / store / free / close`).
  The UI (`lib/main.dart`) already branches on `Platform.isIOS`; voice recall uses Apple
  Speech via the `speech_to_text` package. So the app, not just the engine, is iOS-aware.
- **The engine sources are the single source of truth in `../src/CMakeLists.txt`** (the same
  build Android and Linux use). It is: every `c/*.c` **and** `c/crypto/*.c` **except**
  `main.c` (a library has no `main`), built as **C99**, with `../../../c` on the header
  search path. `crypto/` (`ais_crypto.c` + the vendored `monocypher.c`) is **not optional**:
  leave it out and the app loads but crashes the first time a secret is encrypted or revealed,
  with an undefined `aisc_*` symbol. Mirror that file list exactly.
- **The index lives in the app documents dir** (`getApplicationDocumentsDirectory()/ais`):
  plain-text files on the phone, the same format as every other platform.
- The web/`--serve` loopback path is **not** the iOS client. This is the native FFI app.

## What you have to do

Prereqs: macOS, Xcode, CocoaPods, and (for a real device / TestFlight) an Apple Developer
account. The Simulator needs none of that.

1. `cd app/flutter && flutter pub get`.
2. **Wire the C engine into the Runner target** (the one real task). Two equivalent ways:
   - In Xcode (`open ios/Runner.xcworkspace`): add every `c/*.c` and `c/crypto/*.c` except
     `main.c` to the Runner target's *Compile Sources*; set *Header Search Paths* to include
     `$(SRCROOT)/../../../c`; set the C standard to `c99`; keep default symbol visibility so
     `DynamicLibrary.process()` can resolve them.
   - Or a small podspec that does the same (lists those sources + the header path), referenced
     from `ios/Podfile`. Keep it in lockstep with `../src/CMakeLists.txt` so iOS never drifts
     from the Android/Linux engine build.
3. **Simulator first** (no account): `flutter run -d <simulator-id>`. Exercise recall, store,
   and especially encrypt + reveal (that path links the crypto sources, so a missing file
   surfaces here, not in the store).
4. **Real device:** set the *Signing team* in Xcode to your Apple Developer account, then
   `flutter run -d <device-id>`. (A free Apple ID works but the build expires in 7 days; a
   paid account removes that.)
5. **Ship:** archive in Xcode and upload to App Store Connect / TestFlight. Keep the bundle id
   `com.aisindex.ais`, and bump the version in `pubspec.yaml` to match the release tag.

## Gotchas

- Undefined `aisc_*` at runtime on first encrypt/reveal = a `c/crypto/*.c` file was left out of
  the target. Add all of them (see the no-undefined-crypto assertion in `../src/CMakeLists.txt`).
- If you build the engine as a separate framework instead of compiling into Runner, make sure
  it is actually linked into the final binary, otherwise `process()` finds nothing.
- Stay consistent with the other wrappers: same engine sources, same `embed.h` seam, index in
  the documents dir. Do not fork the engine or special-case it for iOS.

## Nice-to-have, later

- Siri Shortcuts so "Hey Siri, ask AIS to recall ..." hits `recall()` (the same seam a future
  glasses client would ride). See the parent `app/flutter/README.md`.
