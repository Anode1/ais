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
# After that pass it installs the armeabi-v7a RELEASE build and asserts it
# launches and stays up (see the last section).
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
    AIS_ANDROID_MODE=profile AIS_CRUD_KEEP=1 sh "$root/tests/gui/flutter-crud-android.sh" || exit $?

# --- the release build itself: launch and stay up ----------------------------
# The profile build above is compiled like the release but is not the file
# users install. A release APK is not debuggable, so nothing here can read its
# index; the check is that it opens Recent and is still alive, which is what
# build 627 could not do on this ABI. It installs over the profile build (a
# local release is signed with the debug key when android/key.properties is
# absent) and so opens the index the pass above left, which holds one live
# record (AIS_CRUD_KEEP): on an empty index the old binding did not crash.
PKG=com.aisindex.ais
app="$root/app/flutter"
adb32() { "$ADB" -s "$serial" "$@"; }
rlog=$(mktemp "${TMPDIR:-/tmp}/ais_rel32.XXXXXX")
. "$root/tests/gui/android-apk.sh"
apk_build "$app" release android-arm "$rlog" || {
    echo "  FAIL release APK build"; tail -15 "$rlog" | sed 's/^/       /'; rm -f "$rlog"; exit 1; }
rm -f "$rlog"
apk="$app/build/app/outputs/flutter-apk/app-release.apk"
[ -f "$apk" ] || { echo "  FAIL no release APK at $apk"; exit 1; }
if ! adb32 install -r -d --abi armeabi-v7a "$apk" >/dev/null 2>&1; then
    # another signing key (key.properties present): replace the app outright
    adb32 uninstall "$PKG" >/dev/null 2>&1
    adb32 install --abi armeabi-v7a "$apk" >/dev/null 2>&1 || {
        echo "  FAIL could not install the release APK"; exit 1; }
fi
pkginfo=$(adb32 shell dumpsys package "$PKG" 2>/dev/null | tr -d '\r')
case $pkginfo in
    *primaryCpuAbi=armeabi-v7a*) ;;
    *) echo "  FAIL the release APK did not install as armeabi-v7a"; exit 1 ;;
esac
case $pkginfo in
    *DEBUGGABLE*) echo "  FAIL the installed APK is debuggable, so it is not the release build"; exit 1 ;;
esac
adb32 logcat -c >/dev/null 2>&1
adb32 shell am force-stop "$PKG" >/dev/null 2>&1
adb32 shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 20
pid=$(adb32 shell pidof "$PKG" 2>/dev/null | tr -d '\r')
died=$(adb32 logcat -d 2>/dev/null | grep -c "Fatal signal.*$PKG\|Fatal signal.*om.aisindex")
# leave the device as the next run expects it: the release build would refuse
# run-as, and a differently signed one would refuse the profile install
adb32 uninstall "$PKG" >/dev/null 2>&1
if [ -z "$pid" ] || [ "$died" -gt 0 ]; then
    echo "  FAIL the armeabi-v7a release build did not stay up (pid '$pid', $died fatal signals)"
    adb32 shell am broadcast -a android.intent.action.CLOSE_SYSTEM_DIALOGS >/dev/null 2>&1
    exit 1
fi
echo "  ok   the armeabi-v7a release build opens Recent and stays up"
