# Installing AIS on Android

There are two ways to run AIS on a phone. Pick by what you want today:

- **Native app (Flutter)** — a real installed app with an icon. The C engine runs
  on-device via FFI; the index is plain-text files in the app's storage. Best for
  a store-style app and offline use. Voice recall uses the native Android
  `SpeechRecognizer`.
- **PWA (web app) via Termux** — no APK. Run `ais --serve` on the phone and open
  it in Chrome as an installable web app. Fastest path to working voice today,
  since it needs no signing.

Both keep your data on the phone in the same plain-text index format as the
desktop CLI.

## Route A: the native Flutter app

### A1. Install the prebuilt debug APK (quickest)

A debug APK is built at
`app/flutter/build/app/outputs/flutter-apk/app-debug.apk`.

```sh
# on the phone: Settings -> About -> tap Build number 7x to enable Developer
# options, then enable USB debugging. Plug in USB and accept the prompt.

# from the dev machine, install it
adb install app/flutter/build/app/outputs/flutter-apk/app-debug.apk
```

No cable? Copy the `.apk` to the phone (cloud or file transfer), tap it, and allow
"install from this source."

### A2. Build and install from source

```sh
# needs the Flutter SDK + the Android SDK/NDK on PATH (see A4)
cd app/flutter
flutter pub get
flutter devices            # confirm the phone is listed
flutter install            # build + install to the connected device
# or, with live logs:
flutter run -d <device-id>
```

The native build compiles the engine to `libais.so` per ABI (`arm64-v8a` for
real phones, `x86_64` for emulators) via `app/flutter/src/CMakeLists.txt`. The
Dart FFI loads it with `DynamicLibrary.open('libais.so')`.

### A3. Microphone permission

Voice recall needs the mic. The permission is **requested at runtime on the first
tap of the mic button**, not at app launch. Tap the mic, allow the prompt, and
speak; the recognized text fills the search box and runs the recall. If you
deny it, text recall still works and the status line says the mic is unavailable.
(Declared as `RECORD_AUDIO` in `android/app/src/main/AndroidManifest.xml`.)

### A4. Android SDK/NDK on Linux (one-time, for building)

```sh
# easiest: Android Studio bundles SDK + platform-tools + NDK + the license flow
sudo snap install android-studio --classic
# launch once, let it install the SDK, then:
flutter doctor --android-licenses
flutter doctor                 # Android toolchain should show OK
```

Minimal (no IDE): get "Command line tools only" from
<https://developer.android.com/studio>, unzip to
`$ANDROID_HOME/cmdline-tools/latest`, then:

```sh
sdkmanager "platform-tools" "platforms;android-35" "build-tools;35.0.0"
flutter doctor --android-licenses
```

### A5. Status: not store-published yet

The release build is still signed with the debug key
(`signingConfig = signingConfigs.getByName("debug")`). Sideloading to your own
device works now; publishing to Play Store needs a real release keystore and
signing config first.

## Route B: the PWA via Termux

```sh
# install Termux from F-Droid (NOT the abandoned Play Store build):
#   https://f-droid.org/packages/com.termux/

# in Termux on the phone:
pkg install git clang make
git clone https://github.com/Anode1/ais && cd ais && make

# run the local server (binds localhost:8765 ON the phone)
AIS_WEB=app ./ais --serve
```

Then in Chrome on the phone, open `http://localhost:8765`. Because localhost is a
"secure context," the full PWA works: tap the menu, **Add to Home screen /
Install app**, and voice + offline shell are available. The index lives on the
phone under Termux's storage.

Why localhost and not the desktop's IP: browser voice and install require HTTPS
or localhost. Plain `http://<desktop-ip>:8765` from a phone shows the page but
disables voice, install, and the service worker.

## Which route to choose

| Want | Route |
| --- | --- |
| A real installed app icon, mostly text recall now | A (sideload the APK) |
| Voice working today, no signing/build hassle | B (PWA in Termux) |
| To develop/iterate on the native app | A2 (`flutter run`) |

## Troubleshooting

- **`flutter devices` shows nothing**: USB debugging off, or the "Allow USB
  debugging?" prompt was dismissed. Re-enable and replug. `adb devices` should
  list it as `device`, not `unauthorized`.
- **App installs but crashes on open**: usually the native lib. Confirm the build
  produced `libais.so` for your device's ABI (`arm64-v8a` for phones). A debug
  build only carrying `x86_64` is emulator-only.
- **Mic does nothing / no prompt**: confirm `RECORD_AUDIO` is in the manifest and
  the device actually has Google's speech service. The prompt appears on the first
  mic tap; if previously denied, clear it in Settings -> Apps -> ais -> Permissions.
- **Voice on the PWA is greyed out**: you opened it over `http://<ip>` instead of
  `http://localhost` on the phone. Use Termux + localhost, or HTTPS.

## See also

- `app/flutter/README.md` — the native app internals (FFI seam, CMake wiring).
- `app/README.md` — the PWA internals and the secure-context rule.
