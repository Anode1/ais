# app/flutter: AIS native app (Flutter, FFI to the C engine)

The store app for App Store + Play Store. See gui/README.md for why front-ends
stay thin; here that seam is the FFI to the C engine (`../../c`) via
`../../c/embed.h`. Voice recall uses the platform's native speech-to-text (Apple
Speech / Android SpeechRecognizer), which works on iPhone, unlike the browser
PWA.

    lib/ais_ffi.dart   FFI bindings to ais_embed_open/recall/store/free/close
    lib/main.dart      the UI: search + mic + results + put (recall-first)
    src/CMakeLists.txt builds the engine as libais.so for Android & Linux

## One-time: generate the platform folders

This scaffold holds only the Dart + the native CMake. Generate the
`android/ ios/ linux/` runners over it (keeps `lib/` and `pubspec.yaml`):

    cd app/flutter
    flutter create --org com.aisindex --project-name ais --platforms=android,ios,linux .
    flutter pub get

## Wire the native build to the C engine

- **Android**: in `android/app/build.gradle`, point the NDK build at our CMake:

      android {
        defaultConfig { externalNativeBuild { cmake { } } }
        externalNativeBuild { cmake { path "../../src/CMakeLists.txt" } }
      }

  Needs the Android SDK + NDK (doc/android-install.md). Produces `libais.so` per ABI.

- **Linux desktop** (for testing here): in `linux/CMakeLists.txt` add:

      add_subdirectory(../src ais_build)

  and ensure `libais.so` is bundled next to the runner.

- **iOS**: the engine is wired in by `../../ais_engine.podspec` and arrives as
  `ais_engine.framework` in the app bundle. CI builds it unsigned on macOS and
  launches it on a simulator on every change to `c/**` or the app, asserting the
  engine opens an index. Signing, a device and TestFlight are in
  <https://github.com/Anode1/ais/issues/1>.

## Build / run

    flutter run -d linux      # desktop sanity check (uses libais.so)
    flutter run -d <android>  # your Android device
    # iOS: open ios/Runner.xcworkspace on Zoya's Mac, sign, run / TestFlight

Release builds take their version from the git tag, not from `pubspec.yaml`:

    flutter build appbundle --release $(sh tool/version.sh)
    flutter build ipa --release $(sh tool/version.sh)

`tool/version.sh` prints `--build-name`/`--build-number` (store metadata) and the
matching `--dart-define`s (what the About screen shows), all from the nearest
`vX.Y.Z` tag plus the commit count. `pubspec.yaml`'s `version:` is only the
fallback for a plain `flutter run`.

## Android SDK (Linux)

Android SDK setup: see doc/android-install.md.

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
