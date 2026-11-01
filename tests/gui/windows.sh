#!/bin/sh
# windows.sh -- GUI layer: the native Windows build, ais.exe (CLI, web GUI, LAN
# sync over Winsock) and ais-gui.exe (the launcher, gui/win/).
#
# Building needs MinGW-w64; running needs Windows, which native-windows.yml does
# on a Windows runner. With a cross-compiler present this proves both still
# COMPILE and LINK; with none, it SKIPs. Builds in a scratch copy so no Windows
# object file is left in c/ for the next Linux `make`.
#
# Exit 0 = passed, 1 = FAIL, 77 = SKIP. The Windows zip is a release asset, so a
# cross-compile failure is a failure.

root=$(cd "$(dirname "$0")/../.." && pwd)

command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 || {
    echo "  SKIP no MinGW-w64 here (the Windows build runs in CI)"
    exit 77
}

W=$(mktemp -d "${TMPDIR:-/tmp}/ais_win.XXXXXX") || exit 2
trap 'rm -rf "$W"' EXIT
cp -r "$root/c" "$root/gui" "$root/icons" "$W/" && rm -f "$W"/c/ais "$W"/c/ais_ut
find "$W" -name '*.o' -delete        # every Linux object, the crypto/ ones too
rm -f "$W"/gui/win/*.exe "$W"/gui/win/*.res "$W"/gui/win/version.h

if make -C "$W/c" CC=x86_64-w64-mingw32-gcc LDFLAGS=-static >"$W/cli.log" 2>&1 &&
   [ -f "$W/c/ais.exe" ]; then
    echo "  ok   ais.exe cross-compiles (MinGW-w64)"
else
    echo "  FAIL ais.exe cross-compile:"; tail -5 "$W/cli.log"; exit 1
fi

# -W -Wall are in its Makefile; a warning is a defect, as in c/ (STYLE.md).
if make -C "$W/gui/win" CC=x86_64-w64-mingw32-gcc >"$W/gui.log" 2>&1 &&
   [ -f "$W/gui/win/ais-gui.exe" ] && ! grep -q "warning:" "$W/gui.log"; then
    echo "  ok   ais-gui.exe cross-compiles, no warnings"
else
    echo "  FAIL ais-gui.exe cross-compile:"; grep "warning:\|error" "$W/gui.log" | head -5; exit 1
fi
exit 0
