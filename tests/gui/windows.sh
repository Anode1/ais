#!/bin/sh
# windows.sh -- GUI layer: the native Windows build, ais.exe (CLI, web GUI, LAN
# sync over Winsock) and the native window (win32/).
#
# Building needs MinGW-w64; running needs Windows, which native-windows.yml does
# on a Windows runner. With a cross-compiler present this proves both still
# COMPILE and LINK against the engine; with none, it SKIPs. Builds in a scratch
# copy so no Windows object file is left in c/ for the next Linux `make`.
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
cp -r "$root/c" "$root/win32" "$W/" && rm -f "$W"/c/*.o "$W"/c/ais "$W"/c/ais_ut "$W"/win32/*.exe "$W"/win32/*.o

if make -C "$W/c" CC=x86_64-w64-mingw32-gcc LDFLAGS=-static >"$W/cli.log" 2>&1 &&
   [ -f "$W/c/ais.exe" ]; then
    echo "  ok   ais.exe cross-compiles (MinGW-w64)"
else
    echo "  FAIL ais.exe cross-compile:"; tail -5 "$W/cli.log"; exit 1
fi
if make -C "$W/win32" CC=x86_64-w64-mingw32-gcc >"$W/gui.log" 2>&1 &&
   ls "$W"/win32/*.exe >/dev/null 2>&1; then
    echo "  ok   native window cross-compiles (MinGW-w64)"
else
    echo "  FAIL native window cross-compile:"; tail -5 "$W/gui.log"; exit 1
fi
exit 0
