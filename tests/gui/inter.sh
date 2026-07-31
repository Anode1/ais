#!/bin/sh
# inter.sh -- GUI layer: real click-and-assert interaction test of `ais --serve`,
# driven through a minimal Chrome DevTools Protocol client written in C
# (tests/gui/cdp.c). Where ui.sh only asserts the page RENDERS with its controls
# (headless --dump-dom), this one drives the live page: it types a query, presses
# Enter, and asserts the seeded record renders -- the actual input + fetch path.
#
# It compiles cdp.c + cdptest.c with the system cc (no framework, no library),
# seeds a throwaway /tmp index, starts the server headless and a headless Chrome
# with remote debugging, then runs the C driver against both.
#
# Needs: the ais binary, cc, curl, and Chrome/Chromium on PATH.
# Exit 0 = all passed, 1 = a failure, 77 = SKIP (missing toolchain).
#
# Usage:  sh tests/gui/inter.sh [path-to-ais]      (default ./c/ais)

AIS=${1:-./c/ais}
case $AIS in /*) ;; *) AIS=$(cd "$(dirname "$AIS")" && pwd)/$(basename "$AIS") ;; esac
DIR=$(cd "$(dirname "$0")" && pwd)

BR=$(command -v google-chrome-stable || command -v google-chrome \
     || command -v chromium || command -v chromium-browser)
[ -n "$BR" ]                          || { echo "inter: no chrome/chromium on PATH -- SKIP"; exit 77; }
command -v cc   >/dev/null 2>&1       || { echo "inter: no cc -- SKIP"; exit 77; }
command -v curl >/dev/null 2>&1       || { echo "inter: curl not found -- SKIP"; exit 77; }

TMP=$(mktemp -d)
IDX="$TMP/idx"; DDIR="$TMP/chrome"; BIN="$TMP/cdptest"
SPORT=$(( 18000 + ($$ % 2000) ))     # ais --serve
CPORT=$(( 20000 + ($$ % 2000) ))     # chrome remote debugging
SRV= ; CH=
cleanup() {
    [ -n "$SRV" ] && kill "$SRV" 2>/dev/null
    [ -n "$CH" ]  && kill "$CH" 2>/dev/null
    wait 2>/dev/null                 # reap chrome before removing its data dir
    rm -rf "$TMP" 2>/dev/null
}
trap cleanup EXIT

if ! cc -std=c99 -Wall -Wextra -o "$BIN" "$DIR/cdp.c" "$DIR/cdptest.c" 2>"$TMP/cc.log"; then
    echo "  FAIL cdp client did not compile"; cat "$TMP/cc.log"; exit 1
fi

"$AIS" -f "$IDX" --init >/dev/null 2>&1
"$AIS" -f "$IDX" -v "https://example.org/venice" venice >/dev/null 2>&1
"$AIS" -f "$IDX" -v "https://example.org/canal"  venice trip >/dev/null 2>&1
"$AIS" -f "$IDX" --add 2 -v "https://example.org/canal-2" >/dev/null 2>&1  # a 2nd LINK, not a 2nd record
AIS_NO_OPEN=1 "$AIS" -f "$IDX" --serve "$SPORT" >/dev/null 2>&1 &
SRV=$!
"$BR" --headless --disable-gpu --no-sandbox --disable-extensions --no-first-run \
      --no-default-browser-check --remote-debugging-address=127.0.0.1 \
      --remote-debugging-port="$CPORT" --user-data-dir="$DDIR" about:blank \
      >"$TMP/chrome.log" 2>&1 &
CH=$!

S="http://127.0.0.1:$SPORT"; V="http://127.0.0.1:$CPORT/json/version"
i=0; while [ $i -lt 50 ]; do curl -s -o /dev/null "$S/" && break; i=$((i+1)); sleep 0.1; done
i=0; while [ $i -lt 50 ]; do curl -s -o /dev/null "$V"  && break; i=$((i+1)); sleep 0.1; done
if ! curl -s -o /dev/null "$S/"; then echo "  FAIL server did not start on $SPORT"; exit 1; fi
if ! curl -s -o /dev/null "$V";  then echo "  FAIL chrome debug port not up on $CPORT"; exit 1; fi

"$BIN" 127.0.0.1 "$CPORT" "$S/"
rc1=$?

# Second front-end, SAME driver: app/index.html served via $AIS_WEB. It is a
# separate page from the embedded PAGE and had no coverage at all, so a feature
# could land in one and not the other (it had, for the tag actions). The element
# ids are deliberately identical, so one driver asserts both.
APPDIR=$(cd "$DIR/../../app" && pwd)
IDX2="$TMP/idx2"; SPORT2=$(( SPORT + 900 )); SRV2=   # +1 would collide with ui.sh, see there
"$AIS" -f "$IDX2" --init >/dev/null 2>&1
"$AIS" -f "$IDX2" -v "https://example.org/venice" venice >/dev/null 2>&1
"$AIS" -f "$IDX2" -v "https://example.org/canal"  venice trip >/dev/null 2>&1
"$AIS" -f "$IDX2" --add 2 -v "https://example.org/canal-2" >/dev/null 2>&1
AIS_WEB="$APPDIR" AIS_NO_OPEN=1 "$AIS" -f "$IDX2" --serve "$SPORT2" >/dev/null 2>&1 &
SRV2=$!
S2="http://127.0.0.1:$SPORT2"
i=0; while [ $i -lt 50 ]; do curl -s -o /dev/null "$S2/" && break; i=$((i+1)); sleep 0.1; done
if ! curl -s -o /dev/null "$S2/"; then
    echo "  FAIL app-page server did not start on $SPORT2"; kill "$SRV2" 2>/dev/null; exit 1
fi
# prove we are really testing the OTHER page, not the embedded fallback
if curl -s "$S2/" | grep -q 'manifest.webmanifest'; then
    echo "  ok   app page (\$AIS_WEB) is the one being served"
else
    echo "  FAIL \$AIS_WEB did not serve app/index.html (embedded fallback?)"
    kill "$SRV2" 2>/dev/null; exit 1
fi
echo "-- app/index.html (the \$AIS_WEB front end) --"
"$BIN" 127.0.0.1 "$CPORT" "$S2/"
rc2=$?
kill "$SRV2" 2>/dev/null
[ "$rc1" -eq 0 ] && [ "$rc2" -eq 0 ]
