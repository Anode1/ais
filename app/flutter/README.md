# app/flutter: AIS native app (Flutter, FFI to the C engine)

The store app for App Store + Play Store. See gui/README.md for why front-ends
stay thin; here that seam is the FFI to the C engine (`../../c`) via
`../../c/embed.h`. Voice recall uses the platform's native speech-to-text (Apple
Speech / Android SpeechRecognizer), which works on iPhone, unlike the browser
PWA.

    lib/ais_ffi.dart   FFI bindings to ais_embed_open/recall/store/free/close
    lib/main.dart      the UI: search + mic + results + put (recall-first)
    src/CMakeLists.txt builds the engine as libais.so for Android & Linux

## How the engine gets in

The platform runners (`android/ ios/ linux/`) are committed; nothing needs
generating. Each one reaches the same C sources a different way, and all three
lists must stay in step:

- **Android** builds `libais.so` per ABI through the NDK, pointed at
  `src/CMakeLists.txt` from `android/app/build.gradle.kts`. Needs the Android SDK
  and NDK (`../../doc/android-install.md`).
- **Linux desktop** adds the same CMake as a subdirectory from
  `linux/CMakeLists.txt`, so `flutter run -d linux` is a real sanity check of the
  FFI without a phone.
- **iOS** takes the sources through `../../ais_engine.podspec`, which delivers
  them as `ais_engine.framework` in the app bundle. CI builds it unsigned on
  macOS and launches it on a simulator on every change to `c/**` or the app,
  asserting the engine opens an index. Signing, a device and TestFlight are in
  <https://github.com/Anode1/ais/issues/1>.

## Build / run

    flutter run -d linux      # desktop sanity check (uses libais.so)
    flutter run -d <android>  # your Android device

Release builds take their version from the git tag, never from `pubspec.yaml`:

    flutter build appbundle --release $(sh tool/version.sh)
    flutter build ipa --release $(sh tool/version.sh)

Why those flags are not optional, and what `tool/version.sh` derives, is in
[`../../doc/dev/VERSIONING.md`](../../doc/dev/VERSIONING.md). Publishing is
[`../../doc/dev/ANDROID_RELEASE.md`](../../doc/dev/ANDROID_RELEASE.md).

## Notes

- The index lives in the app's documents dir (`getApplicationDocumentsDirectory()/ais`):
  plain-text files on the phone, the same format as everywhere else.
- Nothing goes to any cloud, and both platforms back that up by default, so both are
  turned off explicitly: Android via `allowBackup="false"` +
  `res/xml/data_extraction_rules.xml` (cloud denied, local device-to-device transfer
  allowed), iOS via `NSURLIsExcludedFromBackupKey` on the index dir, set over the
  `ais/backup` channel in `ios/Runner/AppDelegate.swift`. Android also sets
  `hasFragileUserData="true"` so uninstall offers "Keep app data".
- Next steps after first run: Siri Shortcuts (iOS) and Android App Actions so
  "Hey Siri / Hey Google, ask AIS to recall …" hits `recall()`: the same seam a
  future glasses client rides.
