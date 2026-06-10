# app/flutter — AIS native app (Flutter, FFI to the C engine)

The store app for App Store + Play Store. It is a thin client: all logic is the
C engine (`../../c`) reached over the FFI seam (`../../c/embed.h`), so it gives
the same results as the CLI and `ais --serve`. Voice recall uses the platform's
native speech-to-text (Apple Speech / Android SpeechRecognizer) — which works on
iPhone, unlike the browser PWA.

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

- **Android** — in `android/app/build.gradle`, point the NDK build at our CMake:

      android {
        defaultConfig { externalNativeBuild { cmake { } } }
        externalNativeBuild { cmake { path "../../src/CMakeLists.txt" } }
      }

  Needs the Android SDK + NDK (see below). Produces `libais.so` per ABI.

- **Linux desktop** (for testing here) — in `linux/CMakeLists.txt` add:

      add_subdirectory(../src ais_build)

  and ensure `libais.so` is bundled next to the runner.

- **iOS** (on a Mac) — add the `../../c/*.c` files (except `main.c`) to the
  Runner target in Xcode (or a small podspec), with `../../c` on the header
  search path. The FFI uses `DynamicLibrary.process()`, so the symbols just need
  to be in the app binary.

## Build / run

    flutter run -d linux      # desktop sanity check (uses libais.so)
    flutter run -d <android>  # your Android device
    # iOS: open ios/Runner.xcworkspace on Zoya's Mac, sign, run / TestFlight

## Android SDK (Linux)

Easiest — Android Studio bundles the SDK, platform-tools, emulator, and the
license flow:

    sudo snap install android-studio --classic
    # launch once, let it install the SDK, then:
    flutter doctor --android-licenses
    flutter doctor          # should show Android toolchain OK

Minimal (no IDE) — "Command line tools only" from
<https://developer.android.com/studio> → unzip to
`$ANDROID_HOME/cmdline-tools/latest`, then:

    sdkmanager "platform-tools" "platforms;android-35" "build-tools;35.0.0"
    flutter doctor --android-licenses

(`apt install android-sdk` exists but is outdated and laid out wrong for
Flutter — prefer one of the above.) Target API 34/35 (Android 14/15) in 2026.

## Notes

- The index lives in the app's documents dir (`getApplicationDocumentsDirectory()/ais`)
  — plain-text files on the phone, the same format as everywhere else.
- Next steps after first run: Siri Shortcuts (iOS) and Android App Actions so
  "Hey Siri / Hey Google, ask AIS to recall …" hits `recall()` — the same seam a
  future glasses client rides.
