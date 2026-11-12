#!/bin/sh
# flutter-crud-android32.sh -- the app as a 32-bit ARM phone runs it.
#
# Build 627 crashed on every armeabi-v7a phone at the first Recent page: the
# Dart binding declared a C long as 64 bits, and every other device and test
# was 64-bit. This runs flutter-crud-android.sh with the armeabi-v7a build on
# an x86_64 emulator image that translates ARM (Android 11, google_apis: its
# ro.product.cpu.abilist names armeabi-v7a). With the old binding it dies at
# launch with SIGSEGV at 0x64, the page size read as a pointer.
#
# Three traps, each met once:
#  - Google publishes real armeabi-v7a images only up to API 25 and the
#    emulator no longer runs them on an x86_64 host; translation is the way.
#  - The debug build's JIT crashes inside the translator (a DartWorker
#    SIGSEGV in libndk_translation). The profile build is compiled, and still
#    debuggable, so run-as reads the index.
#  - The APK carries libais.so for every ABI, so the device installs its own
#    (x86_64) unless adb install is given --abi.
#
# Setup, once:
#   sdkmanager "system-images;android-30;google_apis;x86_64"
#   avdmanager create avd -n ais_test32 -k "system-images;android-30;google_apis;x86_64" -d pixel_4
#
# The device is $AIS_ANDROID32_SERIAL (default emulator-5556). If it is not
# attached, AIS_ANDROID_BOOT=1 boots the AVD $AIS_AVD32 (default ais_test32)
# headless on that port.
#
# Exit 0 = pass, 1 = fail, 77 = SKIP (no device, no AVD, no toolchain).

root=$(cd "$(dirname "$0")/../.." && pwd)
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
ADB="$SDK/platform-tools/adb"
EMU="$SDK/emulator/emulator"
serial=${AIS_ANDROID32_SERIAL:-emulator-5556}
avd=${AIS_AVD32:-ais_test32}

command -v flutter >/dev/null 2>&1 || { echo "  SKIP no flutter SDK"; exit 77; }
[ -x "$ADB" ] || { echo "  SKIP no adb (\$ANDROID_SDK_ROOT)"; exit 77; }

booted_here=""
cleanup() { [ -n "$booted_here" ] && "$ADB" -s "$serial" emu kill >/dev/null 2>&1; return 0; }
trap cleanup EXIT INT TERM

if ! "$ADB" devices | awk 'NR>1 && $2=="device" {print $1}' | grep -qx "$serial"; then
    [ -n "${AIS_ANDROID_BOOT:-}" ] || { echo "  SKIP $serial not attached (AIS_ANDROID_BOOT=1 to boot $avd)"; exit 77; }
    [ -x "$EMU" ] || { echo "  SKIP no emulator"; exit 77; }
    "$EMU" -list-avds 2>/dev/null | grep -qx "$avd" || { echo "  SKIP no AVD $avd (see the setup lines in this script)"; exit 77; }
    port=${serial#emulator-}
    "$EMU" -avd "$avd" -port "$port" -no-window -no-audio -no-boot-anim -no-snapshot \
           -gpu swiftshader_indirect >/dev/null 2>&1 &
    booted_here=1
    i=0
    while [ $i -lt 90 ]; do
        [ "$("$ADB" -s "$serial" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ] && break
        i=$((i + 1)); sleep 5
    done
    [ "$("$ADB" -s "$serial" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ] || {
        echo "  SKIP $avd did not finish booting"; exit 77; }
fi

"$ADB" -s "$serial" shell getprop ro.product.cpu.abilist 2>/dev/null | grep -q 'armeabi-v7a' || {
    echo "  SKIP $serial does not run armeabi-v7a"; exit 77; }

# a crash dialog left by an earlier run covers the app and hides every control
"$ADB" -s "$serial" shell am broadcast -a android.intent.action.CLOSE_SYSTEM_DIALOGS >/dev/null 2>&1

ANDROID_SERIAL=$serial AIS_ANDROID_TP=android-arm AIS_ANDROID_WANT_ABI=armeabi-v7a \
    AIS_ANDROID_MODE=profile sh "$root/tests/gui/flutter-crud-android.sh"
