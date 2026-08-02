#!/bin/sh
# flutter-host-android.sh -- drive the REAL Android app as the sync HOST.
#
# WHY THIS EXISTS. flutter-sync-android.sh proves the app can JOIN: a CLI peer
# runs `--sync --serve`, the app is handed an ais:// link and taps Join. The
# opposite half -- the app hosting while something else joins it -- was driven by
# nothing at all. serve.sh does exercise POST /api/sync/host, but that is the
# desktop web GUI over HTTP, a different front end entirely. So the flow two
# friends with two phones actually use, one taps Host and shows a QR, had never
# run end to end on either side.
#
# The camera half genuinely needs a human. The HOST half does not: the app prints
# the address and token as text beside the QR ("Or type the address and token on
# the other device"), so a CLI peer can join it exactly as a second phone would.
#
# It reaches the app's listener through `adb forward`, since the emulator's own
# LAN address is not routable from the host.
#
# Nodes are located by uiautomator TEXT, never by screen coordinates. The
# percentage-based taps in the sibling script are the reason a layout change can
# silently stop testing anything, which AGENTS.md records having been bitten by.
#
# NON-DESTRUCTIVE: merges one proof record and asserts it crossed. Set
# AIS_ANDROID_CLEAR=1 to wipe the app's data first (that DELETES what is there).
#
# Exit 0 = pass, 1 = fail, 77 = SKIP (no emulator/toolchain, or no host address).

set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
app="$root/app/flutter"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
ADB="$SDK/platform-tools/adb"
EMU="$SDK/emulator/emulator"
PKG=com.aisindex.ais
FWD="${AIS_HOST_FWD_PORT:-8901}"
PROOF_CLI="PROOF-host-from-cli-$$"

command -v flutter >/dev/null 2>&1 || { echo "  SKIP no flutter SDK"; exit 77; }
[ -x "$ADB" ] || { echo "  SKIP no adb (\$ANDROID_SDK_ROOT)"; exit 77; }
[ -x "$root/c/ais" ] || { echo "  SKIP c/ais not built"; exit 77; }

work=$(mktemp -d "${TMPDIR:-/tmp}/ais_host.XXXXXX")
booted_here=""
cleanup() {
    "$ADB" forward --remove "tcp:$FWD" >/dev/null 2>&1 || true
    [ -n "$booted_here" ] && kill "$booted_here" 2>/dev/null
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

dev=$("$ADB" devices | awk 'NR>1 && $2=="device" {print $1; exit}')
if [ -z "$dev" ]; then
    [ -n "${AIS_ANDROID_BOOT:-}" ] || { echo "  SKIP no device attached (AIS_ANDROID_BOOT=1 to boot an AVD)"; exit 77; }
    [ -x "$EMU" ] || { echo "  SKIP no emulator and no device attached"; exit 77; }
    avd=$("$EMU" -list-avds 2>/dev/null | head -1)
    [ -n "$avd" ] || { echo "  SKIP no AVD defined"; exit 77; }
    "$EMU" -avd "$avd" -no-window -no-audio -no-boot-anim -no-snapshot \
           -gpu swiftshader_indirect >"$work/emu.log" 2>&1 &
    booted_here=$!
    i=0
    while [ $i -lt 90 ]; do
        [ "$("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ] && break
        i=$((i + 1)); sleep 5
    done
    [ "$("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ] || {
        echo "  SKIP emulator did not finish booting"; exit 77; }
fi

# --- uiautomator helpers: find a node by its visible text, tap its centre -----
ui_dump() {
    "$ADB" shell uiautomator dump /sdcard/ais_ui.xml >/dev/null 2>&1 || return 1
    "$ADB" shell cat /sdcard/ais_ui.xml 2>/dev/null | tr -d '\r'
}

# node_bounds XML NEEDLE -> "x1 y1 x2 y2" for the first node whose text or
# content-desc contains NEEDLE
node_bounds() {
    printf '%s' "$1" | tr '<' '\n' |
    grep -F "$2" |
    grep -o 'bounds="\[[0-9]*,[0-9]*\]\[[0-9]*,[0-9]*\]"' |
    head -1 |
    sed 's/bounds="\[//; s/\]\[/ /; s/\]"//; s/,/ /g'
}

# tap_text NEEDLE [tries] -- wait for a node to appear, then tap its centre
tap_text() {
    needle=$1; tries=${2:-10}
    i=0
    while [ $i -lt "$tries" ]; do
        b=$(node_bounds "$(ui_dump)" "$needle" || true)
        if [ -n "$b" ]; then
            set -- $b
            "$ADB" shell input tap $(( ($1 + $3) / 2 )) $(( ($2 + $4) / 2 )) >/dev/null 2>&1
            return 0
        fi
        i=$((i + 1)); sleep 2
    done
    return 1
}

pass=0; fail=0
ok()   { pass=$((pass+1)); echo "  ok   $1"; }
bad()  { fail=$((fail+1)); echo "  FAIL $1"; }

# --- build and install the shipped artifact ----------------------------------
abi=$("$ADB" shell getprop ro.product.cpu.abi | tr -d '\r')
case $abi in
    x86_64)      tp=android-x64 ;;
    arm64-v8a)   tp=android-arm64 ;;
    armeabi-v7a) tp=android-arm ;;
    *)           echo "  SKIP unknown device ABI $abi"; exit 77 ;;
esac
( cd "$app" && flutter build apk --debug --target-platform "$tp" ) >"$work/build.log" 2>&1 || {
    echo "  FAIL APK build"; tail -15 "$work/build.log" | sed 's/^/       /'; exit 1; }
apk=$(find "$app/build/app/outputs" -name '*debug*.apk' | head -1)
[ -n "$apk" ] || { echo "  FAIL no APK produced"; exit 1; }
"$ADB" install -r -d "$apk" >"$work/install.log" 2>&1 || {
    echo "  FAIL could not install the APK"; tail -5 "$work/install.log" | sed 's/^/       /'; exit 1; }
[ -n "${AIS_ANDROID_CLEAR:-}" ] && "$ADB" shell pm clear "$PKG" >/dev/null 2>&1

# --- open the app and reach Host through the menus, by text ------------------
"$ADB" shell am force-stop "$PKG" >/dev/null 2>&1 || true
"$ADB" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 12

tap_text 'Settings' 10       || { echo "  SKIP could not find the Settings menu"; exit 77; }
sleep 2
tap_text 'Sync &amp; backup' 8 || tap_text 'Sync' 8 || { echo "  SKIP could not open the sync sheet"; exit 77; }
sleep 2
tap_text 'Host a sync' 8     || { echo "  SKIP could not find 'Host a sync'"; exit 77; }
sleep 6

# --- read the address and token the app is showing ---------------------------
xml=$(ui_dump)
line=$(printf '%s' "$xml" | tr '<' '\n' | grep -o 'ais --sync http://[0-9.]*:[0-9]* --token [0-9a-f]*' | head -1)
if [ -z "$line" ]; then
    case $xml in
        *"Wi-Fi address"*) echo "  SKIP the emulator has no LAN address, so the app cannot host"; exit 77 ;;
    esac
    bad "the host screen never printed an address and token"
    printf '%s' "$xml" | tr '<' '\n' | grep -o 'text="[^"]*"' | head -20 | sed 's/^/       /'
    echo "host: $pass passed, $fail failed"; exit 1
fi
ok "host: the app prints a joinable address and token"
aport=$(printf '%s' "$line" | sed 's/.*:\([0-9]*\) --token.*/\1/')
atok=$(printf '%s' "$line" | sed 's/.*--token //')

# the emulator's own LAN address is not routable from here; adb forwards instead
"$ADB" forward "tcp:$FWD" "tcp:$aport" >/dev/null 2>&1 || {
    bad "adb forward tcp:$FWD -> tcp:$aport failed"; echo "host: $pass passed, $fail failed"; exit 1; }
ok "host: the app is listening on port $aport"

# --- a CLI peer joins the phone, exactly as a second phone would -------------
"$root/c/ais" -f "$work/peer" --init >/dev/null 2>&1
"$root/c/ais" -f "$work/peer" -v "$PROOF_CLI" hostproof >/dev/null 2>&1
before=$("$root/c/ais" -f "$work/peer" --dump 2>/dev/null | grep -c . || true)

out=$("$root/c/ais" -f "$work/peer" --sync "http://127.0.0.1:$FWD" --token "$atok" 2>&1) || true
case $out in
    *converged*) ok "host: the CLI peer joined the phone and converged" ;;
    *) bad "the CLI peer could not join the phone: $out" ;;
esac

# a WRONG token against a hosting phone must be refused, or the QR secret is decor
badout=$("$root/c/ais" -f "$work/peer2" --sync "http://127.0.0.1:$FWD" \
         --token 00000000000000000000000000000000 2>&1) || true
case $badout in
    *converged*) bad "a wrong token was accepted by the hosting app" ;;
    *) ok "host: a wrong token is refused by the hosting app" ;;
esac

# --- both directions ---------------------------------------------------------
sleep 4
"$ADB" shell run-as "$PKG" cat app_flutter/ais/store 2>/dev/null | grep -q "$PROOF_CLI" \
    && ok "converge: the phone received the CLI record" \
    || bad "the phone never received the CLI record"

after=$("$root/c/ais" -f "$work/peer" --dump 2>/dev/null | grep -c . || true)
if [ "$after" -gt "$before" ]; then
    ok "converge: the CLI peer received records from the phone ($before -> $after)"
else
    bad "nothing came back from the phone ($before -> $after)"
fi

echo "host: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
