#!/bin/sh
# windows.sh -- GUI layer: the native Win32 GUI (win32/ais-gui.c).
#
# Building it needs MinGW-w64; driving its UI needs a Windows desktop. Where a
# MinGW cross-compiler is present (CI), this at least proves the GUI still
# COMPILES against the engine. With no MinGW (a normal POSIX box, this env), it
# reports SKIP -- the real click/assert UI test belongs on a Windows runner, or a
# C CDP client under tests/ (see tests/shot/README.md).
#
# Exit 0 = passed, 1 = failed, 77 = SKIP.

root=$(cd "$(dirname "$0")/../.." && pwd)

if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    if make -C "$root/c" CC=x86_64-w64-mingw32-gcc AIS_STD=-std=gnu99 >/dev/null 2>&1 &&
       make -C "$root/win32" CC=x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
        echo "  ok   win32 GUI cross-compiles (MinGW-w64)"
        exit 0
    fi
    echo "  FAIL win32 GUI cross-compile (MinGW-w64)"
    exit 1
fi

echo "  SKIP no MinGW-w64 here (win32 build runs in CI; UI run needs Windows)"
exit 77
