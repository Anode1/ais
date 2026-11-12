#!/bin/sh
# flutter-crud-android.sh -- the everyday loop, driven through the REAL app on a
# device: ADD a record, CHANGE its tags, DELETE it, then a record under an
# elapsed due key: the DUE group heads Recent and Done clears it. Each step is
# asserted against the app's own index rather than against the screen, except
# the Due group itself, which only the screen can show.
#
# The other two Android layers cover sync. This one covers what every user does
# every day, and what nothing else tested: the paths where a Flutter dialog owns
# a text field, an overflow menu owns an action, and an armed delete has to reach
# the engine. A delete that never committed unless something else happened to
# flush it shipped once precisely because no test pressed Delete and then left.
#
# Nodes are found by uiautomator TEXT (or by class, for the unlabelled text
# fields), never by fixed screen coordinates, so a layout change cannot silently
# stop this from asserting anything.
#
# DESTRUCTIVE ONLY OF ITS OWN RECORDS: it adds two records with unique markers,
# edits, clears and deletes those, and touches nothing else on the device.
# AIS_DUE_SHOT=PATH keeps a screenshot with the Due group on screen.
#
# Exit 0 = pass, 1 = fail, 77 = SKIP (no device, no flutter toolchain).

set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
app="$root/app/flutter"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
ADB="$SDK/platform-tools/adb"
EMU="$SDK/emulator/emulator"
PKG=com.aisindex.ais
MARK="CRUD-$$"                 # unique per run: two runs never confuse each other
TAG1="crudtag$$"
TAG2="crudmore$$"

command -v flutter >/dev/null 2>&1 || { echo "  SKIP no flutter SDK"; exit 77; }
[ -x "$ADB" ] || { echo "  SKIP no adb (\$ANDROID_SDK_ROOT)"; exit 77; }

work=$(mktemp -d "${TMPDIR:-/tmp}/ais_crud.XXXXXX")
booted_here=""
cleanup() {
    [ -n "$booted_here" ] && kill "$booted_here" 2>/dev/null
    rm -rf "$work"
    return 0
}
trap cleanup EXIT INT TERM

dev=$("$ADB" devices | awk 'NR>1 && $2=="device" {print $1; exit}')
# Two devices attached (the 32-bit emulator beside the usual one) make a bare
# adb refuse; pin the first unless the caller named one.
[ -z "$dev" ] || { ANDROID_SERIAL=${ANDROID_SERIAL:-$dev}; export ANDROID_SERIAL; }
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

# --- uiautomator helpers -----------------------------------------------------
ui_dump() {
    "$ADB" shell uiautomator dump /sdcard/ais_crud.xml >/dev/null 2>&1 || return 1
    "$ADB" shell cat /sdcard/ais_crud.xml 2>/dev/null | tr -d '\r'
}

# node_bounds XML NEEDLE -> "x1 y1 x2 y2" of the first node carrying NEEDLE
node_bounds() {
    printf '%s' "$1" | tr '<' '\n' |
    grep -F "$2" |
    grep -o 'bounds="\[[0-9]*,[0-9]*\]\[[0-9]*,[0-9]*\]"' |
    head -1 |
    sed 's/bounds="\[//; s/\]\[/ /; s/\]"//; s/,/ /g'
}

# nth_field_bounds XML N -> bounds of the Nth EditText. The Add sheet's fields
# carry no text of their own until they hold some, so this is the one place
# where position is unavoidable -- position among the FIELDS, which is stable,
# not position on the screen, which is not.
nth_field_bounds() {
    printf '%s' "$1" | tr '<' '\n' |
    grep -F 'class="android.widget.EditText"' |
    grep -o 'bounds="\[[0-9]*,[0-9]*\]\[[0-9]*,[0-9]*\]"' |
    sed -n "$2p" |
    sed 's/bounds="\[//; s/\]\[/ /; s/\]"//; s/,/ /g'
}

tap_bounds() { set -- $1; "$ADB" shell input tap $(( ($1 + $3) / 2 )) $(( ($2 + $4) / 2 )) >/dev/null 2>&1; }

# tap_text NEEDLE [tries]
tap_text() {
    needle=$1; tries=${2:-8}
    i=0
    while [ $i -lt "$tries" ]; do
        b=$(node_bounds "$(ui_dump)" "$needle" || true)
        if [ -n "$b" ]; then tap_bounds "$b"; return 0; fi
        i=$((i + 1)); sleep 2
    done
    return 1
}

# tap_field N -- tap the Nth text field of whatever is on screen
tap_field() {
    b=$(nth_field_bounds "$(ui_dump)" "$1" || true)
    [ -n "$b" ] || return 1
    tap_bounds "$b"
}

# type_into N TEXT -- focus the Nth field, type, and CHECK what landed. `input
# text` fired straight after `input tap` loses the first character or two while
# the field takes focus, and a UI test that does not read back what it typed
# reports the app's fault for its own. Retries once, then gives up loudly.
type_into() {
    n=$1; text=$2; try=0
    while [ $try -lt 2 ]; do
        tap_field "$n" || return 1
        sleep 2
        "$ADB" shell input text "$text" >/dev/null 2>&1
        sleep 2
        if ui_dump | grep -qF "$text"; then return 0; fi
        # clear whatever partial text landed, then try once more
        i=0; while [ $i -lt 40 ]; do "$ADB" shell input keyevent 67 >/dev/null 2>&1; i=$((i+1)); done
        try=$((try + 1))
    done
    return 1
}

store()  { "$ADB" shell run-as "$PKG" cat app_flutter/ais/store 2>/dev/null; }
tomb()   { "$ADB" shell run-as "$PKG" cat app_flutter/ais/tomb 2>/dev/null; }
ktomb()  { "$ADB" shell run-as "$PKG" cat app_flutter/ais/ktomb 2>/dev/null; }

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok   $1"; }
bad() { fail=$((fail+1)); echo "  FAIL $1"
        "$ADB" exec-out screencap -p > "$work/fail-$fail.png" 2>/dev/null || true; }

# --- build and install the shipped artifact ----------------------------------
abi=$("$ADB" shell getprop ro.product.cpu.abi 2>/dev/null | tr -d '\r')
[ -n "$abi" ] || { echo "  SKIP device not reachable (adb returned nothing)"; exit 77; }
case $abi in
    x86_64)      tp=android-x64 ;;
    arm64-v8a)   tp=android-arm64 ;;
    armeabi-v7a) tp=android-arm ;;
    *)           echo "  SKIP unknown device ABI $abi"; exit 77 ;;
esac
# AIS_ANDROID_TP builds for another ABI the device also runs: android-arm on an
# x86_64 image that translates ARM is the app as a 32-bit phone runs it
# (flutter-crud-android32.sh).
tp=${AIS_ANDROID_TP:-$tp}
# AIS_ANDROID_MODE=profile builds compiled code: the debug build's JIT dies
# inside the ARM translator. A profile APK is still debuggable, so run-as works.
mode=${AIS_ANDROID_MODE:-debug}
( cd "$app" && flutter build apk "--$mode" --target-platform "$tp" ) >"$work/build.log" 2>&1 || {
    echo "  FAIL APK build"; tail -15 "$work/build.log" | sed 's/^/       /'; exit 1; }
apk=$(find "$app/build/app/outputs" -name "*$mode*.apk" | head -1)
[ -n "$apk" ] || { echo "  FAIL no APK produced"; exit 1; }
# The debug APK carries libais.so for every ABI, so the device would pick its
# own; --abi makes it install and run the one under test.
"$ADB" install -r -d ${AIS_ANDROID_WANT_ABI:+--abi "$AIS_ANDROID_WANT_ABI"} "$apk" >"$work/install.log" 2>&1 || {
    echo "  FAIL could not install the APK"; tail -5 "$work/install.log" | sed 's/^/       /'; exit 1; }
if [ -n "${AIS_ANDROID_WANT_ABI:-}" ]; then
    got=$("$ADB" shell dumpsys package "$PKG" 2>/dev/null | tr -d '\r' |
          sed -n 's/.*primaryCpuAbi=//p' | head -1)
    [ "$got" = "$AIS_ANDROID_WANT_ABI" ] || {
        echo "  FAIL the app installed as '$got', not $AIS_ANDROID_WANT_ABI"; exit 1; }
    echo "  ok   the app runs as $got"
fi

"$ADB" shell am force-stop "$PKG" >/dev/null 2>&1 || true
"$ADB" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 10

# --- ADD ---------------------------------------------------------------------
# Either entry point is correct: the empty view offers its own button and hides
# the fab, a populated one shows the fab. Exactly ONE of them is on screen, which
# is itself worth asserting -- two Adds side by side has been a bug twice.
xml=$(ui_dump)
adds=$(printf '%s' "$xml" | tr '<' '\n' | grep -cE 'content-desc="(Add|Add something)"' || true)
[ "$adds" = "1" ] && ok "add: exactly one Add control on screen" \
                  || bad "add: expected one Add control, found $adds"

tap_text 'Add something' 3 || tap_text 'Add' 5 || { echo "  FAIL no Add control"; exit 1; }
sleep 3
type_into 1 "$MARK" || { echo "  FAIL could not type into the Add sheet"; exit 1; }
type_into 2 "$TAG1" || { echo "  FAIL could not type the tags"; exit 1; }
tap_text 'Save' 5 || { echo "  FAIL no Save button"; exit 1; }
sleep 4

store | grep -q "$MARK" && ok "add: the record reached the index" \
                        || bad "add: the record never reached the index"
store | grep "$MARK" | grep -q "$TAG1" && ok "add: with the tag that was typed" \
                                       || bad "add: the tag did not land on it"

# --- UPDATE (tags) -----------------------------------------------------------
ui_dump | grep -qF "$MARK" && ok "add: and the app shows it in the list" \
                           || bad "add: it is in the index but not on screen"
tap_text 'More' 6 || { echo "  FAIL no row menu"; exit 1; }
sleep 2
tap_text 'Edit tags' 5 || { echo "  FAIL no 'Edit tags' action"; exit 1; }
sleep 3
type_into 1 "$TAG2" || { echo "  FAIL could not type into the tag editor"; exit 1; }
tap_text 'Add' 4 || true          # the chip editor's own add, when it has one
sleep 1
tap_text 'Apply' 5 || tap_text 'Save' 5 || { echo "  FAIL no Apply in the tag editor"; exit 1; }
sleep 4

store | grep "$MARK" | grep -q "$TAG2" && ok "update: the new tag is on the record" \
                                       || bad "update: the new tag never landed"
store | grep "$MARK" | grep -q "$TAG1" && ok "update: and the old tag survived it" \
                                       || bad "update: the old tag was lost"

# --- DELETE ------------------------------------------------------------------
# Press Delete and then LEAVE, inside the Undo window. The engine must have been
# told by the time the app is out of the foreground: a delete that waited for
# something else to flush it came back on the next launch.
tomb_before=$(tomb | grep -c . || true)
tap_text 'More' 6 || { echo "  FAIL no row menu for delete"; exit 1; }
sleep 2
tap_text 'Delete' 5 || { echo "  FAIL no Delete action"; exit 1; }
sleep 1
"$ADB" shell input keyevent KEYCODE_HOME >/dev/null 2>&1
sleep 4

tomb_after=$(tomb | grep -c . || true)
[ "${tomb_after:-0}" -gt "${tomb_before:-0}" ] \
    && ok "delete: leaving the app settled it, without waiting for anything else" \
    || bad "delete: nothing was tombstoned (an armed delete was lost)"

"$ADB" shell am force-stop "$PKG" >/dev/null 2>&1 || true
"$ADB" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 10
ui_dump | grep -q "$MARK" && bad "delete: the record is back after a restart" \
                          || ok "delete: and it is still gone after a restart"

# --- DUE ---------------------------------------------------------------------
# A record under an elapsed due key heads Recent as the "Due" group (GUI.md),
# loaded with Recent on open. Filed through the app's own Add sheet with the
# key due:2020, seen after a relaunch, cleared with Done: the key is detached
# when the Undo window lapses, and the group stays gone after a restart.
DUE="DUE-$$"
tap_text 'Add something' 3 || tap_text 'Add' 5 || { echo "  FAIL no Add control for the due record"; exit 1; }
sleep 3
type_into 1 "$DUE" || { echo "  FAIL could not type the due record"; exit 1; }
type_into 2 "due:2020" || { echo "  FAIL could not type the due key"; exit 1; }
tap_text 'Save' 5 || { echo "  FAIL no Save button for the due record"; exit 1; }
sleep 4
store | grep "$DUE" | grep -q "due:2020" && ok "due: the record is filed under due:2020" \
                                         || bad "due: the key did not land on the record"

"$ADB" shell am force-stop "$PKG" >/dev/null 2>&1 || true
"$ADB" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
# Flutter exposes a Text to uiautomator as content-desc, never as text=, and
# the first dump after a launch can precede the semantics tree, so look more
# than once before calling it absent.
i=0; seen=
while [ $i -lt 8 ]; do
    ui_dump | grep -q 'content-desc="Due"' && { seen=1; break; }
    i=$((i + 1)); sleep 2
done
[ -n "$seen" ] && ok "due: the Due group heads Recent after a relaunch" \
               || bad "due: no Due group on screen"
"$ADB" exec-out screencap -p > "${AIS_DUE_SHOT:-$work/due.png}" 2>/dev/null || true

# The group is first in the list, so the first row menu on screen is its row's.
tap_text 'More' 6 || { echo "  FAIL no row menu on the due row"; exit 1; }
sleep 2
tap_text 'Done' 5 || { echo "  FAIL no Done action"; exit 1; }
sleep 1
ui_dump | grep -q 'content-desc="Due"' && bad "due: the row did not leave the group at once" \
                                || ok "due: Done took the row out of the group at once"
sleep 6                                   # past the 4.5s Undo window: the detach is sent
ktomb | grep -q "due:2020" && ok "due: the detach reached the index (ktomb)" \
                           || bad "due: nothing in ktomb (the armed Done was lost)"
"$ADB" shell am force-stop "$PKG" >/dev/null 2>&1 || true
"$ADB" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 10
ui_dump | grep -q 'content-desc="Due"' && bad "due: the group is back after a restart (the detach never reached the index)" \
                                || ok "due: and it stays gone after a restart"

# Leave the device as found: the due record is this run's, delete it as above.
tap_text 'More' 6 || { echo "  FAIL no row menu to delete the due record"; exit 1; }
sleep 2
tap_text 'Delete' 5 || { echo "  FAIL no Delete action for the due record"; exit 1; }
sleep 1
"$ADB" shell input keyevent KEYCODE_HOME >/dev/null 2>&1
sleep 4
"$ADB" shell am force-stop "$PKG" >/dev/null 2>&1 || true
"$ADB" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 10
ui_dump | grep -q "$DUE" && bad "due: the due record is back after its delete" \
                         || ok "due: the due record is gone"

# AIS_CRUD_KEEP=1 leaves ONE live record behind, on purpose: the 32-bit layer
# then opens this index with the release build, and an empty Recent never
# reaches the code that crashed there. Only for a device the caller owns.
if [ -n "${AIS_CRUD_KEEP:-}" ]; then
    KEEP="KEEP-$$"
    tap_text 'Add something' 3 || tap_text 'Add' 5 || { echo "  FAIL no Add control for the kept record"; exit 1; }
    sleep 3
    type_into 1 "$KEEP" || { echo "  FAIL could not type the kept record"; exit 1; }
    type_into 2 "$TAG1" || { echo "  FAIL could not type the kept record's tag"; exit 1; }
    tap_text 'Save' 5 || { echo "  FAIL no Save button for the kept record"; exit 1; }
    sleep 4
    store | grep -q "$KEEP" && ok "keep: one live record is left for the next check" \
                            || bad "keep: the kept record never reached the index"
fi

if [ "$fail" -eq 0 ]; then
    echo "  ok   flutter add / update / delete / due on Android (asserted on the index)"
    exit 0
fi
echo "       screenshots of the failure(s) are in $work (kept)"
trap - EXIT
exit 1
