#!/bin/sh
# flutter-sync.sh -- GUI layer: the NATIVE Flutter app's sync flow, end to end.
#
# Wraps app/flutter/uitest/run.sh, which builds the Linux desktop app, drives the
# real Host/Join UI under Xvfb with xdotool, and asserts a record crosses in both
# directions against a CLI peer.
#
# The desktop toolchain (ninja, clang, GTK headers) is not everywhere, so an
# absent one SKIPs rather than failing the suite.
#
# Exit 0 = passed, 1 = failed, 77 = SKIP.

root=$(cd "$(dirname "$0")/../.." 2>/dev/null && pwd)
ui="$root/app/flutter/uitest/run.sh"

[ -f "$ui" ] || { echo "  SKIP app/flutter/uitest/run.sh not found"; exit 77; }

# run.sh is a BASH script (set -o pipefail, arrays): running it with `sh` fails
# instantly on a dash /bin/sh, and this layer had skipped everywhere it was
# tried, so nothing noticed until it finally ran on a CI runner.
command -v bash >/dev/null 2>&1 || { echo "  SKIP no bash (uitest/run.sh needs it)"; exit 77; }
command -v flutter >/dev/null 2>&1 || { echo "  SKIP no flutter SDK"; exit 77; }
command -v xdotool >/dev/null 2>&1 || { echo "  SKIP no xdotool (X input)"; exit 77; }
command -v import  >/dev/null 2>&1 || { echo "  SKIP no ImageMagick import (screenshots)"; exit 77; }
command -v xvfb-run >/dev/null 2>&1 || { echo "  SKIP no xvfb-run"; exit 77; }
# The Linux desktop build needs these beyond the SDK; without them `flutter build
# linux` fails in a second and looks like a product bug, not a missing toolchain.
command -v ninja >/dev/null 2>&1 || { echo "  SKIP no ninja (flutter build linux)"; exit 77; }
{ command -v clang >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1; } \
    || { echo "  SKIP no clang (flutter build linux)"; exit 77; }
pkg-config --exists gtk+-3.0 2>/dev/null \
    || { echo "  SKIP no gtk+-3.0 dev headers (flutter build linux)"; exit 77; }

out=$(bash "$ui" 2>&1)
rc=$?
if [ "$rc" = 0 ]; then
    echo "  ok   flutter sync UI (Host/Join converges with a CLI peer)"
    exit 0
fi
echo "  FAIL flutter sync UI"
echo "$out" | sed 's/^/       /'
exit 1
