#!/bin/sh
#
# shot.sh - screenshot a rendered web page to a PNG using headless Chrome.
# A dev-only helper (not built, not shipped) so an agent or a human can SEE the
# `ais --serve` web GUI instead of guessing from the HTML/JS in c/serve.c.
#
# Shell only -- ais is a C project, so the only other "language" here is /bin/sh.
# Viewport capture only; the GUI is one short page, so that is enough. Full-page
# capture and click-and-assert UI tests, if ever needed, belong in C under tests/
# (a small Chrome DevTools Protocol client driven by the C suite), not in another
# runtime -- see README.md, "CDP / UI tests".
#
# Usage:
#   ./shot.sh URL [OUT.png] [WIDTHxHEIGHT]
#
# Examples (start --serve headless with AIS_NO_OPEN=1 so it does not pop a window):
#   ./shot.sh http://127.0.0.1:8080/                # -> ./shot.png, 1280x1024
#   ./shot.sh http://127.0.0.1:8080/ gui.png 460x820
#
#######################################################

URL=$1
OUT=${2:-shot.png}
SIZE=${3:-1280x1024}

if [ -z "$URL" ]; then
	echo "usage: $0 URL [OUT.png] [WIDTHxHEIGHT]" >&2
	exit 2
fi

# pick whichever Chrome binary exists
BROWSER=$(command -v google-chrome-stable || command -v google-chrome || command -v chromium || command -v chromium-browser)
if [ -z "$BROWSER" ]; then
	echo "no chrome/chromium binary found on PATH" >&2
	exit 1
fi

W=${SIZE%x*}
H=${SIZE#*x}

# disposable profile so we can run while a normal Chrome is open
PROFILE=$(mktemp -d)
trap 'rm -rf "$PROFILE"' EXIT

# --virtual-time-budget lets the page load + run its JS before the snapshot (ms);
# the modern equivalent of the old "sleep 5".
"$BROWSER" \
	--headless=new \
	--disable-gpu \
	--hide-scrollbars \
	--no-first-run \
	--user-data-dir="$PROFILE" \
	--window-size="$W,$H" \
	--virtual-time-budget=5000 \
	--screenshot="$OUT" \
	"$URL" >/dev/null 2>&1

if [ -s "$OUT" ]; then
	echo "wrote $OUT ($SIZE)"
else
	echo "capture failed: no image written" >&2
	exit 1
fi
