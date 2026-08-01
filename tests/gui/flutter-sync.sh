#!/bin/sh
# flutter-sync.sh -- GUI layer: the NATIVE Flutter app's sync flow, end to end.
#
# Wraps app/flutter/uitest/run.sh, which builds the Linux desktop app, drives the
# real Host/Join UI under Xvfb with xdotool, and asserts a record crosses in both
# directions against a CLI peer. That harness existed but was wired into nothing,
# so when the Sync control moved into the overflow menu it silently stopped
# testing the sync flow at all -- for weeks, across three releases, while the sync
# and merge code underneath it was being changed. A test nothing runs is not a
# test. This layer is what makes that drift fail out loud.
#
# The desktop toolchain (ninja, clang, GTK headers) is not everywhere, so an
# absent one SKIPs like every other GUI layer rather than failing the suite.
#
# Exit 0 = passed, 1 = failed, 77 = SKIP.

root=$(cd "$(dirname "$0")/../.." 2>/dev/null && pwd)
ui="$root/app/flutter/uitest/run.sh"

[ -f "$ui" ] || { echo "  SKIP app/flutter/uitest/run.sh not found"; exit 77; }

command -v flutter >/dev/null 2>&1 || { echo "  SKIP no flutter SDK"; exit 77; }
command -v xdotool >/dev/null 2>&1 || { echo "  SKIP no xdotool (X input)"; exit 77; }
command -v import  >/dev/null 2>&1 || { echo "  SKIP no ImageMagick import (screenshots)"; exit 77; }
command -v xvfb-run >/dev/null 2>&1 || { echo "  SKIP no xvfb-run"; exit 77; }
# The Linux desktop build needs these beyond the SDK itself; without them
# `flutter build linux` fails in about a second and the run would look like a
# product bug rather than a missing toolchain.
command -v ninja >/dev/null 2>&1 || { echo "  SKIP no ninja (flutter build linux)"; exit 77; }
{ command -v clang >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1; } \
    || { echo "  SKIP no clang (flutter build linux)"; exit 77; }
pkg-config --exists gtk+-3.0 2>/dev/null \
    || { echo "  SKIP no gtk+-3.0 dev headers (flutter build linux)"; exit 77; }

out=$(sh "$ui" 2>&1)
rc=$?
if [ "$rc" = 0 ]; then
    echo "  ok   flutter sync UI (Host/Join converges with a CLI peer)"
    exit 0
fi
echo "  FAIL flutter sync UI"
echo "$out" | sed 's/^/       /'
exit 1
