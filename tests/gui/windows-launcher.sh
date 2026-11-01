#!/bin/sh
# windows-launcher.sh -- GUI layer: ais-gui.exe, the Windows launcher (gui/win/),
# run on Windows. $1 is the launcher; the ais.exe it starts is the one beside it.
#
# Checks what a double-click does: the server comes up on 8765, a second start
# while one runs exits at once (the page again, not a second server), --quit
# ends the launcher and the server, --quit with none running exits 1.
# AIS_NO_OPEN keeps the browser closed, as for `ais --serve`.
#
# Needs Windows (Git Bash, as native-windows.yml has); elsewhere SKIP.
# Exit 0 = passed, 1 = FAIL, 77 = SKIP.

G=${1:-}
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) ;;
    *) echo "  SKIP needs Windows (native-windows.yml runs it)"; exit 77 ;;
esac
[ -n "$G" ] && [ -f "$G" ] || { echo "  SKIP no launcher at '$G' (make -C gui/win)"; exit 77; }

URL=http://127.0.0.1:8765/api/version
export AIS_NO_OPEN=1
export MSYS_NO_PATHCONV=1      # keep /FI and friends as switches, not paths

ais_running() { tasklist /FI "IMAGENAME eq ais.exe" 2>/dev/null | grep -qi "ais.exe"; }
wait_port() {  # wait_port up|down : up to 20 s
    i=0
    while [ $i -lt 100 ]; do
        if curl -fs "$URL" >/dev/null 2>&1; then [ "$1" = up ] && return 0
        else [ "$1" = down ] && return 0; fi
        sleep 0.2; i=$((i + 1))
    done
    return 1
}

if curl -fs "$URL" >/dev/null 2>&1; then
    echo "  FAIL something already answers on 8765"; exit 1
fi
# a failure must not leave the launcher up: it holds the mutex the installer
# test checks, and the next step would fail for the wrong reason. When --quit
# itself is what failed, kill both, loudly.
# the launcher writes beside the user's temp files (GetTempPath), which is
# %LOCALAPPDATA%\Temp here; Git Bash's own $TEMP is its /tmp, another place
log=$(cygpath -u "$LOCALAPPDATA")/Temp/ais-serve.log
trap '"$G" --quit >/dev/null 2>&1 || { echo "  (killing the launcher and the server by force)";
      taskkill /IM ais-gui.exe /F >/dev/null 2>&1; taskkill /IM ais.exe /F >/dev/null 2>&1; }' EXIT
if "$G" --quit; then
    echo "  FAIL --quit with no launcher running exited 0"; exit 1
fi

"$G" &
if ! wait_port up; then
    echo "  FAIL port 8765 did not answer in 20 s"
    tail -20 "$log" 2>/dev/null
    exit 1
fi
echo "  ok   ais-gui.exe started ais --serve: $(curl -fs "$URL")"

# the second double-click: must return at once, with one server still up
if ! timeout 20 "$G"; then
    echo "  FAIL a second launcher did not exit at once"; "$G" --quit; exit 1
fi
curl -fs "$URL" >/dev/null || { echo "  FAIL the server went away after the second start"; exit 1; }
echo "  ok   a second start exits at once, the server stays"

"$G" --quit || {
    echo "  FAIL --quit found no launcher"
    tasklist /FI "IMAGENAME eq ais-gui.exe"; tasklist /FI "IMAGENAME eq ais.exe"
    tail -20 "$log" 2>/dev/null
    exit 1
}
if ! wait_port down; then
    echo "  FAIL the server still answers 20 s after --quit"; exit 1
fi
sleep 1
if ais_running; then
    echo "  FAIL ais.exe still runs after --quit"; exit 1
fi
echo "  ok   --quit ended the launcher and the server"
exit 0
