#!/usr/bin/env bash
# Xvfb UI test for the Flutter desktop app: drives the real Host/Join sync UI in
# a headless X server and asserts a record crosses to/from a CLI peer. Renderer-
# agnostic (pixels in, X events out), so it works where uiautomator is blind to
# Flutter's single Skia surface. Same spirit as mincdp, for the native surface.
#
#   ./run.sh            # headless: Xvfb on :99, one pass, assert, tear down
#   HEADED=1 ./run.sh   # watch it on your own $DISPLAY instead of Xvfb
#   KEEP=1 ./run.sh     # leave stores + screenshots for inspection
#
# Needs: Xvfb xdotool (X input) and ImageMagick `import` (screenshots), plus a
# Flutter linux toolchain. On Debian/Ubuntu: sudo apt install xvfb xdotool imagemagick
set -euo pipefail

# --- config -----------------------------------------------------------------
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"          # repo root (this file is app/flutter/uitest/)
CLI="$ROOT/c/ais"                             # the C engine CLI (the sync peer)
APP="$ROOT/app/flutter/build/linux/x64/debug/bundle/ais"
DISP="${DISPLAY_XVFB:-:99}"                   # Xvfb display when headless
SCREEN="1400x900x24"                          # Xvfb virtual screen (>= the app window)
PORT="${PORT:-8766}"                          # CLI host / app join port
TIMEOUT="${TIMEOUT:-30}"                       # seconds the peer waits for the join
WORK="${WORK_DIR:-/tmp/ais-uitest}"            # FIXED path (not mktemp): the store line
rm -rf "$WORK"; mkdir -p "$WORK"               # is shown in the UI, so a stable path keeps
SHOTS="$WORK/shots"; mkdir -p "$SHOTS"         # the "sync" link and dialogs at stable coords
PROOF_CLI="PROOF-from-cli-42"                  # seeded on the peer, must reach the app
PROOF_APP="PROOF-from-app-99"                  # seeded on the app, must reach the peer

# Click targets for the pinned 1280x720 window at (0,0), tuned against the fixed
# store path above (the "sync" link follows the store-path text, so the path must
# be fixed for these to hold). Re-tune from $SHOTS if the layout changes; or
# graduate to integration_test Keys (see README) if it churns often.
SYNC_LINK="271 142"                            # the "sync" link on the main screen
JOIN_BTN="831 405"                             # "Join" in the role sheet
ADDR_FIELD="640 327"                           # "Address" field (pre-filled "http://")
TOKEN_FIELD="640 367"                          # "Token" field
SYNC_CONFIRM="823 456"                          # "Sync" (confirm) in the Join dialog

# --- teardown ---------------------------------------------------------------
PIDS=()
cleanup() {
  for p in "${PIDS[@]:-}"; do kill "$p" 2>/dev/null || true; done
  if [ -n "${KEEP:-}" ]; then echo "kept: $WORK"; else rm -rf "$WORK"; fi
}
trap cleanup EXIT

# --- build ------------------------------------------------------------------
[ -x "$CLI" ] || make -C "$ROOT/c" >/dev/null
if [ ! -x "$APP" ]; then
  echo "building Flutter linux app..."
  ( cd "$ROOT/app/flutter" && flutter build linux --debug )
fi

# --- display ----------------------------------------------------------------
# Force the GTK/Flutter app onto X11. On a Wayland session GTK prefers
# WAYLAND_DISPLAY and ignores DISPLAY (and Xvfb), so it renders on the real
# screen where xdotool cannot reach it. x11 => it uses our Xvfb when headless,
# or XWayland on the real display when HEADED (still xdotool-drivable).
export GDK_BACKEND=x11
unset WAYLAND_DISPLAY
if [ -n "${HEADED:-}" ]; then
  : "${DISPLAY:?HEADED=1 needs a real \$DISPLAY}"
else
  Xvfb "$DISP" -screen 0 "$SCREEN" +extension GLX >/dev/null 2>&1 & PIDS+=("$!")
  export DISPLAY="$DISP"
  # Flutter draws through EGL/OpenGL; Xvfb has no GPU, so force Mesa's software
  # rasterizer (llvmpipe) or the surface renders solid black and nothing paints.
  export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
  for _ in $(seq 1 50); do xdotool getdisplaygeometry >/dev/null 2>&1 && break; sleep 0.1; done
fi

# --- seed both stores (isolated; the app finds .ais/ by walking up from its CWD) --
PEER="$WORK/peer/.ais"; APPDIR="$WORK/app/.ais"
mkdir -p "$PEER" "$APPDIR"
"$CLI" -f "$PEER"   --init >/dev/null
"$CLI" -f "$APPDIR" --init >/dev/null
"$CLI" -f "$PEER"   syncproof -v "$PROOF_CLI" >/dev/null
"$CLI" -f "$APPDIR" syncproof -v "$PROOF_APP" >/dev/null

# --- CLI peer hosts; scrape the one-time token it prints --------------------
HOSTLOG="$WORK/host.log"
"$CLI" -f "$PEER" --sync --serve "$PORT" >"$HOSTLOG" 2>&1 & PIDS+=("$!")
TOKEN=""
for _ in $(seq 1 50); do
  TOKEN="$(grep -oE -- '--token [0-9a-f]{32}' "$HOSTLOG" | awk '{print $2}' | head -1)"
  [ -n "$TOKEN" ] && break; sleep 0.1
done
[ -n "$TOKEN" ] || { echo "FAIL: peer never printed a token"; cat "$HOSTLOG"; exit 1; }
echo "peer hosting on :$PORT, token $TOKEN"

# --- launch the app, pin its window -----------------------------------------
( cd "$WORK/app" && exec "$APP" ) >"$WORK/app.log" 2>&1 & PIDS+=("$!")
WIN=""
for _ in $(seq 1 100); do WIN="$(xdotool search --sync --name '^ais$' 2>/dev/null | head -1)"; [ -n "$WIN" ] && break; sleep 0.1; done
[ -n "$WIN" ] || { echo "FAIL: app window never appeared"; cat "$WORK/app.log"; exit 1; }
# Bare Xvfb has no window manager, so windowactivate (_NET_ACTIVE_WINDOW) fails.
# windowfocus is a direct XSetInputFocus and needs none; the app is a single X
# window (Flutter paints dialogs into the same surface), so this one focus is
# enough for every keystroke to reach it.
xdotool windowmove "$WIN" 0 0 windowsize "$WIN" 1280 720 2>/dev/null || true
xdotool windowfocus "$WIN" 2>/dev/null || true
sleep 1

# --- drive: one action per step, screenshot after each ----------------------
step=0
shot() { step=$((step+1)); import -window root "$SHOTS/$(printf '%02d' $step)-$1.png"; }
tap()  { xdotool mousemove --sync $1 click 1; sleep 0.6; shot "$2"; }

shot start
tap "$SYNC_LINK"   sync-sheet          # open the role sheet
tap "$JOIN_BTN"    join-dialog         # choose Join
tap "$ADDR_FIELD"  addr-focus          # focus Address (pre-filled "http://")
xdotool key --clearmodifiers ctrl+a    # select-all so the type replaces, not appends
xdotool type --delay 20 "http://127.0.0.1:$PORT"; shot addr-typed
tap "$TOKEN_FIELD" token-focus         # focus Token
xdotool type --delay 20 "$TOKEN"; shot token-typed
tap "$SYNC_CONFIRM" syncing            # confirm -> pull + merge
sleep "$((TIMEOUT>10?10:TIMEOUT))"; shot done

# --- assert: the record crossed in BOTH directions --------------------------
rc=0
grep -q "$PROOF_CLI" "$APPDIR/store" || { echo "FAIL: app never received $PROOF_CLI"; rc=1; }
grep -q "$PROOF_APP" "$PEER/store"   || { echo "FAIL: peer never received $PROOF_APP"; rc=1; }
if [ "$rc" = 0 ]; then echo "PASS: records converged both ways (screenshots in $SHOTS)"; else cat "$HOSTLOG"; fi
[ -n "${KEEP:-}" ] && echo "screenshots: $SHOTS"
exit "$rc"
