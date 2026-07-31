#!/bin/sh
# ui.sh -- GUI layer: browser render test of the `ais --serve` web page. Drives real
# headless Chrome against a live server on a throwaway /tmp index and asserts on the
# RENDERED DOM (after the page's JS runs) -- the browser-side complement to serve.sh,
# which asserts the /api JSON. Breadth, read-only: the page loads and its controls exist
# by id (the same rule as any UI test: anchor on ids, never presentational classes).
#
# This is the static render cut (controls exist post-JS). The click-and-assert
# complement -- typing a query and asserting the result renders -- is inter.sh,
# driven by the C CDP client (tests/gui/cdp.c). See tests/README.md.
#
# Needs: the ais binary, curl (to wait for the server), and Chrome/Chromium on PATH.
# Exit 0 = all passed, 1 = a failure, 77 = SKIP (no browser or no curl).
#
# Usage:  sh tests/gui/ui.sh [path-to-ais]      (default ./c/ais)

AIS=${1:-./c/ais}
case $AIS in /*) ;; *) AIS=$(cd "$(dirname "$AIS")" && pwd)/$(basename "$AIS") ;; esac

BR=$(command -v google-chrome-stable || command -v google-chrome \
     || command -v chromium || command -v chromium-browser)
[ -n "$BR" ] || { echo "ui: no chrome/chromium on PATH -- SKIP"; exit 77; }
command -v curl >/dev/null 2>&1 || { echo "ui: curl not found -- SKIP"; exit 77; }

IDX=$(mktemp -d)
PORT=$(( 18000 + ($$ % 2000) ))
SRV=
SRV2=
cleanup() { [ -n "$SRV" ] && kill "$SRV" 2>/dev/null
            [ -n "$SRV2" ] && kill "$SRV2" 2>/dev/null; rm -rf "$IDX"; }
trap cleanup EXIT

"$AIS" -f "$IDX" --init >/dev/null 2>&1
"$AIS" -f "$IDX" -v "https://example.org/venice" venice >/dev/null 2>&1
AIS_NO_OPEN=1 "$AIS" -f "$IDX" --serve "$PORT" >/dev/null 2>&1 &
SRV=$!

B="http://127.0.0.1:$PORT"
i=0; while [ $i -lt 50 ]; do curl -s -o /dev/null "$B/" && break; i=$((i+1)); sleep 0.1; done
if ! curl -s -o /dev/null "$B/"; then echo "  FAIL server did not start on $PORT"; exit 1; fi

# render the page in headless Chrome; capture the post-JS DOM
DOM=$("$BR" --headless --disable-gpu --no-sandbox --virtual-time-budget=3000 \
            --dump-dom "$B/" 2>/dev/null)

pass=0; fail=0
has() {  # has LABEL NEEDLE
    case "$DOM" in
        *"$2"*) pass=$((pass + 1)); echo "  ok   $1" ;;
        *)      fail=$((fail + 1)); echo "  FAIL $1 (missing '$2')" ;;
    esac
}

if [ "$(printf '%s' "$DOM" | wc -c)" -gt 2000 ]; then
    pass=$((pass + 1)); echo "  ok   ui: page rendered (non-trivial DOM)"
else
    fail=$((fail + 1)); echo "  FAIL ui: page did not render (empty DOM)"
fi

has "ui: title is AIS"              "<title>AIS</title>"
has "ui: search box (#q)"           'id="q"'
has "ui: value input (#v)"          'id="v"'
has "ui: save button (#save)"       'id="save"'
has "ui: encrypt toggle (#enc)"     'id="enc"'
has "ui: add button (#addbtn)"      'id="addbtn"'
has "ui: index selector (#store)"   'id="store"'
has "ui: detail sheet (#sheet)"     'id="sheet"'
has "ui: timeline range (#tlrange)" 'id="tlrange"'
has "ui: nav has Timeline"          "Timeline"
has "ui: nav has Tags"              "Tags"
# the destructive tag sheet and its three guards: the escape hatch to untag, the
# type-to-confirm input, and the confirm button that ships DISABLED.
has "ui: delete-under sheet (#dsheet)"   'id="dsheet"'
has "ui: sheet is a dialog"              'role="dialog"'
has "ui: escape hatch to untag (#dskeep)" 'id="dskeep"'
has "ui: type-to-confirm input (#dsname)" 'id="dsname"'
has "ui: confirm button (#dsgo)"          'id="dsgo"'
has "ui: confirm ships disabled"          'id="dsgo" class="danger-btn" disabled'

# the Tags view is reachable by hash, so its two per-tag controls can be asserted
TDOM=$("$BR" --headless --disable-gpu --no-sandbox --virtual-time-budget=3000 \
             --dump-dom "$B/#tags" 2>/dev/null)
tag() {  # tag LABEL NEEDLE
    case "$TDOM" in
        *"$2"*) pass=$((pass + 1)); echo "  ok   $1" ;;
        *)      fail=$((fail + 1)); echo "  FAIL $1 (missing '$2')" ;;
    esac
}
tag "ui: #tags opens the Tags view"      "tagrow"
tag "ui: the safe action names the TAG"  "Remove tag"
# the destructive label must name the RECORDS and their count, never the tag:
# that wording is the whole defence against confusing the two.
tag "ui: the destructive action names the RECORDS" "Delete 1 record"

# --- the OTHER front end: app/index.html, served via $AIS_WEB -------------
# It is a separate page from the embedded PAGE and had no render coverage, so a
# control could exist in one and not the other (it did, for the tag actions and
# the edit sheet). Same ids, so the same assertions apply.
APPDIR=$(cd "$(dirname "$0")/../../app" && pwd)
# +1 collides: these scripts run back-to-back, so their PIDs (and thus their base
# ports) usually differ by one, and one script's SECOND server lands on the next
# script's FIRST. Disjoint offsets instead.
IDX2=$(mktemp -d); PORT2=$(( PORT + 500 ))
"$AIS" -f "$IDX2" -v "https://example.org/venice" venice >/dev/null 2>&1
# a DOCUMENT: stored as aisdoc:<base64>, so the page has to decode it to show it
printf 'line one\nline two\nline three\n' | "$AIS" -f "$IDX2" --doc notes >/dev/null 2>&1
AIS_WEB="$APPDIR" AIS_NO_OPEN=1 "$AIS" -f "$IDX2" --serve "$PORT2" >/dev/null 2>&1 &
SRV2=$!
B2="http://127.0.0.1:$PORT2"
i=0; while [ $i -lt 50 ]; do curl -s -o /dev/null "$B2/" && break; i=$((i+1)); sleep 0.1; done
if ! curl -s -o /dev/null "$B2/"; then
    fail=$((fail + 1)); echo "  FAIL app: server did not start on $PORT2"
fi
# a SECRET: only the running server can make one (?enc=1 encrypts server-side),
# and the crypto module may not be built -- "saved 0 record(s)" then, so check.
curl -s -X POST --data-binary 'pw123
wifi-Staff-2026' "$B2/api/put?keys=wifi&enc=1" >/dev/null 2>&1
ENC=no
case $(curl -s "$B2/api/timeline?count=20") in *aisc:*) ENC=yes ;; esac

ADOM=$("$BR" --headless --disable-gpu --no-sandbox --virtual-time-budget=3000 \
             --dump-dom "$B2/#tags" 2>/dev/null)
# the record list lives in the Timeline view, so the value-rendering assertions
# need their own dump
ADOM2=$("$BR" --headless --disable-gpu --no-sandbox --virtual-time-budget=3000 \
              --dump-dom "$B2/#timeline" 2>/dev/null)
# (server torn down after the sw.js fetch below)
app() {  # app LABEL NEEDLE
    case "$ADOM" in
        *"$2"*) pass=$((pass + 1)); echo "  ok   $1" ;;
        *)      fail=$((fail + 1)); echo "  FAIL $1 (missing '$2')" ;;
    esac
}
tl() {   # tl LABEL NEEDLE          -- present in the Timeline render
    case "$ADOM2" in
        *"$2"*) pass=$((pass + 1)); echo "  ok   $1" ;;
        *)      fail=$((fail + 1)); echo "  FAIL $1 (missing '$2')" ;;
    esac
}
tlno() { # tlno LABEL NEEDLE        -- must NOT reach the screen
    case "$ADOM2" in
        *"$2"*) fail=$((fail + 1)); echo "  FAIL $1 (found '$2')" ;;
        *)      pass=$((pass + 1)); echo "  ok   $1" ;;
    esac
}
# prove $AIS_WEB served the app page and not the embedded fallback
app "app: \$AIS_WEB serves app/index.html"  "manifest.webmanifest"
app "app: delete-under sheet (#dsheet)"     'id="dsheet"'
app "app: escape hatch to untag (#dskeep)"  'id="dskeep"'
app "app: type-to-confirm input (#dsname)"  'id="dsname"'
app "app: confirm ships disabled"           'id="dsgo" class="danger-btn" disabled'
app "app: edit sheet (#editsheet)"          'id="editsheet"'
app "app: the chip key editor (#edchips)"   'id="edchips"'
app "app: the store edit field (#storepath)" 'id="storepath"'
app "app: the undo toast (#toast)"          'id="toast"'
app "app: the clean-up control (#cleanbtn)" 'id="cleanbtn"'
app "app: the clean-up control (#cleanbtn)" 'id="cleanbtn"'
app "app: the Undo control (#toastundo)"    'id="toastundo"'
app "app: the safe action names the TAG"    "Remove tag"
app "app: the destructive one names RECORDS" "Delete 1 record"
# the Add sheet can encrypt, like the embedded page's
app "app: encrypt toggle (#enc)"            'id="enc"'
app "app: passphrase field (#pp)"           'id="pp"'
# A DOCUMENT is stored base64-encoded; the page decodes it. Both halves matter:
# the content on screen, and the marker NOT on screen. Neither string exists in
# the page source, so only the render can put them there.
tl "app: a document renders its text"        "line one"
tl "app: it keeps its line breaks"           "line three"
tlno "app: the base64 never reaches the screen" "bGluZSBvbmU"
# A SECRET renders as a lock + Reveal, never as its ciphertext. '>Reveal</button>'
# is the rendered button: the source only holds the string it is built from.
if [ "$ENC" = yes ]; then
    tl "app: a secret offers Reveal"             ">Reveal</button>"
    tlno "app: the ciphertext never reaches the screen" "QUlTLUNSMQ"
else
    echo "  skip app: secret rendering (crypto not built)"
fi
# the service worker must never cache /api/ (live data) and must not be
# cache-first for the shell, or an installed app freezes on the page it shipped with
SW=$(curl -s "$B2/sw.js" 2>/dev/null)
case "$SW" in
    *"/api/"*) pass=$((pass + 1)); echo "  ok   app: the worker excludes /api/ from the cache" ;;
    *)         fail=$((fail + 1)); echo "  FAIL app: the worker does not exclude /api/" ;;
esac
# NETWORK-FIRST, not cache-first: the old worker also contained "/api/", so that
# check alone passed with the fix absent. These two exist only in the new one.
case "$SW" in
    *"clients.claim"*) pass=$((pass + 1)); echo "  ok   app: the worker claims open clients" ;;
    *) fail=$((fail + 1)); echo "  FAIL app: the worker does not claim clients (stale app)" ;;
esac
if printf '%s' "$SW" | grep -q "respondWith($"  ||
   printf '%s' "$SW" | grep -A2 "respondWith(" | grep -q "fetch(e.request)"; then
    pass=$((pass + 1)); echo "  ok   app: the worker goes to the network first"
else
    fail=$((fail + 1)); echo "  FAIL app: the worker is cache-first (an install would freeze)"
fi
kill "$SRV2" 2>/dev/null; rm -rf "$IDX2"

echo "ui: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
