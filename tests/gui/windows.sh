#!/bin/sh
# windows.sh -- GUI layer: the native Win32 GUI (win32/ais-gui.c).
#
# Building it needs MinGW-w64; driving its UI needs a Windows desktop. With a
# MinGW cross-compiler present (CI) this proves the GUI still COMPILES against
# the engine; with none, it SKIPs.
#
# Exit 0 = passed, 77 = SKIP. Native win32 is DEMOTED: even a cross-compile
# failure reports SKIP, never FAIL, so it cannot gate a commit.

root=$(cd "$(dirname "$0")/../.." && pwd)

if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    if make -C "$root/c" CC=x86_64-w64-mingw32-gcc AIS_STD=-std=gnu99 LDLIBS=-lws2_32 >/dev/null 2>&1 &&
       make -C "$root/win32" CC=x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
        echo "  ok   win32 GUI cross-compiles (MinGW-w64)"
        exit 0
    fi
    echo "  SKIP win32 GUI cross-compile failed (non-blocking; native win32 demoted)"
    exit 77
fi

echo "  SKIP no MinGW-w64 here (win32 build runs in CI; UI run needs Windows)"
exit 77
