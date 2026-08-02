#!/bin/sh
# flutter-sync-android.sh -- drive the REAL Android app through a real sync.
#
# WHY ANDROID AND NOT THE DESKTOP BUILD. app/flutter/uitest/run.sh drives the
# Linux desktop build with xdotool, which needs clang + ninja + libgtk-3-dev.
# On a Pop!_OS box those cannot be installed without downgrading the running
# desktop's Wayland libraries, so that harness cannot run at all there -- and an
# APK is what users actually install, so this is the more honest target anyway.
#
# What it proves, end to end, on the shipped artifact:
#   - the ais:// deep link opens the app and prefills Join (the scan-to-pair path)
#   - the FFI seam, the sync isolate and the merge all work on a real device
#   - records cross in BOTH directions against a CLI peer on the host
#
# The emulator reaches the host at 10.0.2.2. The app's index is app-private, so
# assertions read it back with `run-as`, which works because this is a debug build.
#
# NON-DESTRUCTIVE by default: it merges one proof record into whatever index the
# emulator already holds and asserts that record crossed. Set AIS_ANDROID_CLEAR=1
# to wipe the app's data first for a clean-room run -- that DELETES whatever is on
# the emulator, so it is opt-in.
#
# Exit 0 = pass, 1 = fail, 77 = SKIP (no emulator/toolchain).

set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
app="$root/app/flutter"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
ADB="$SDK/platform-tools/adb"
EMU="$SDK/emulator/emulator"
PKG=com.aisindex.ais
PORT="${AIS_ANDROID_PORT:-8899}"
PROOF_CLI="PROOF-from-cli-$$"

command -v flutter >/dev/null 2>&1 || { echo "  SKIP no flutter SDK"; exit 77; }
[ -x "$ADB" ] || { echo "  SKIP no adb (\$ANDROID_SDK_ROOT)"; exit 77; }
[ -x "$root/c/ais" ] || { echo "  SKIP c/ais not built"; exit 77; }

# An already-running device is used as-is; otherwise boot an AVD headlessly.
booted_here=""
dev=$("$ADB" devices | awk 'NR>1 && $2=="device" {print $1; exit}')
if [ -z "$dev" ]; then
    # Booting an AVD costs a minute or two, which is too much to spend on every
    # `make ut`. With nothing attached this SKIPs; set AIS_ANDROID_BOOT=1 to have
    # it start one (what CI, or a deliberate pre-release run, would do).
    [ -n "${AIS_ANDROID_BOOT:-}" ] || { echo "  SKIP no device attached (AIS_ANDROID_BOOT=1 to boot an AVD)"; exit 77; }
    [ -x "$EMU" ] || { echo "  SKIP no emulator and no device attached"; exit 77; }
    avd=$("$EMU" -list-avds 2>/dev/null | head -1)
    [ -n "$avd" ] || { echo "  SKIP no AVD defined"; exit 77; }
    # -no-window is truly headless: no X server involved at all.
    "$EMU" -avd "$avd" -no-window -no-audio -no-boot-anim -no-snapshot \
           -gpu swiftshader_indirect >/tmp/ais_emu.$$.log 2>&1 &
    booted_here=$!
    i=0
    while [ $i -lt 90 ]; do
        [ "$("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ] && break
        i=$((i + 1)); sleep 5
    done
    [ "$("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ] || {
        echo "  SKIP emulator did not finish booting"; kill "$booted_here" 2>/dev/null; exit 77; }
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/ais_androidsync.XXXXXX")
peer_pid=""
cleanup() {
    # Every step is best-effort AND the function ends in `true`. The peer is
    # single-shot, so by the time we get here it has usually exited on its own
    # and `kill` fails -- and in dash the EXIT trap's last status overrides the
    # script's, which turned a passing run into "ok" followed by exit 1.
    [ -n "$peer_pid" ] && kill "$peer_pid" 2>/dev/null || true
    [ -n "$booted_here" ] && "$ADB" emu kill >/dev/null 2>&1 || true
    rm -rf "$work" || true
    return 0
}
trap cleanup EXIT

# The ABI has to match the device, or the app dies on launch with a missing
# libflutter.so and the failure looks like a product bug rather than a build flag.
abi=$("$ADB" shell getprop ro.product.cpu.abi | tr -d '\r')
case "$abi" in
    x86_64)      plat=android-x64 ;;
    arm64-v8a)   plat=android-arm64 ;;
    armeabi-v7a) plat=android-arm ;;
    *)           echo "  SKIP unsupported device ABI '$abi'"; exit 77 ;;
esac

( cd "$app" && flutter build apk --debug --target-platform "$plat" ) >"$work/build.log" 2>&1 || {
    echo "  FAIL could not build the debug APK"; sed 's/^/       /' "$work/build.log" | tail -8; exit 1; }
"$ADB" install -r "$app/build/app/outputs/flutter-apk/app-debug.apk" >"$work/install.log" 2>&1 || {
    echo "  FAIL could not install the APK"; sed 's/^/       /' "$work/install.log" | tail -5; exit 1; }

if [ -n "${AIS_ANDROID_CLEAR:-}" ]; then
    "$ADB" shell pm clear "$PKG" >/dev/null 2>&1 || true
fi

# A CLI peer on the host, holding one record the phone has never seen.
"$root/c/ais" -f "$work/peer" --init >/dev/null
"$root/c/ais" -f "$work/peer" -v "$PROOF_CLI" prooftag >/dev/null
"$root/c/ais" -f "$work/peer" --sync --serve "$PORT" >"$work/peer.log" 2>&1 &
peer_pid=$!
sleep 3
tok=$(grep -o 'token [0-9a-f]*' "$work/peer.log" | awk '{print $2}')
[ -n "$tok" ] || { echo "  FAIL the CLI peer never printed a token"; cat "$work/peer.log"; exit 1; }

# Start the app, then hand it the pairing link the QR would have carried.
"$ADB" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 12
"$ADB" shell am start -a android.intent.action.VIEW \
       -d "ais://sync?host=10.0.2.2%3A$PORT\&token=$tok" >/dev/null 2>&1
sleep 6

# Confirm the prefilled Join dialog. Centred on the dialog, so it moves with the
# screen size rather than with the page layout; recompute from the real display.
size=$("$ADB" shell wm size | tr -d '\r' | awk '{print $3}')
sw=${size%x*}; sh=${size#*x}
"$ADB" shell input tap $((sw * 748 / 1000)) $((sh * 647 / 1000))
sleep 12

rc=0
"$ADB" shell run-as "$PKG" cat app_flutter/ais/store 2>/dev/null | grep -q "$PROOF_CLI" || {
    echo "  FAIL the peer's record never reached the app"; rc=1; }
"$root/c/ais" -f "$work/peer" --dump 2>/dev/null | grep -q "$PROOF_CLI" || {
    echo "  FAIL the peer lost its own record"; rc=1; }
# the phone had records of its own only if its index was not just cleared
if [ -z "${AIS_ANDROID_CLEAR:-}" ]; then
    n=$("$root/c/ais" -f "$work/peer" --stats 2>/dev/null | awk '/^records:/{print $2}')
    [ "${n:-0}" -gt 1 ] || { echo "  FAIL nothing came back from the app"; rc=1; }
fi

if [ "$rc" = 0 ]; then
    echo "  ok   flutter sync on Android (deep link -> Join -> both directions converge)"
else
    "$ADB" exec-out screencap -p > "$work/fail.png" 2>/dev/null
    echo "       peer log:"; sed 's/^/       /' "$work/peer.log"
    echo "       a screenshot of the failure is in $work (kept)"
    trap - EXIT
    [ -n "$peer_pid" ] && kill "$peer_pid" 2>/dev/null
fi
exit $rc
