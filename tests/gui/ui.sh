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
cleanup() { [ -n "$SRV" ] && kill "$SRV" 2>/dev/null; rm -rf "$IDX"; }
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

echo "ui: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
