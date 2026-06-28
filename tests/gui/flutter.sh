#!/bin/sh
# flutter.sh -- GUI layer: the Flutter app (app/flutter).
#
# If the app has unit/widget tests (a test/ dir) and the Flutter SDK is present,
# run them. Otherwise -- the usual case so far -- `dart analyze` the app, which
# catches the FFI binding and widget errors without needing the full SDK or a
# device. With no Dart at all, SKIP.
#
# Exit 0 = passed, 1 = failed, 77 = SKIP.

app=$(cd "$(dirname "$0")/../../app/flutter" 2>/dev/null && pwd)
[ -n "$app" ] || { echo "  SKIP app/flutter not found"; exit 77; }
cd "$app" || { echo "  SKIP cannot enter app/flutter"; exit 77; }

if [ -d test ] && command -v flutter >/dev/null 2>&1; then
    if flutter test >/dev/null 2>&1; then echo "  ok   flutter test"; exit 0; fi
    echo "  FAIL flutter test"; exit 1
fi

if command -v dart >/dev/null 2>&1; then
    if dart analyze lib >/dev/null 2>&1; then
        echo "  ok   dart analyze lib (no test/ dir -- analyze only)"; exit 0
    fi
    echo "  FAIL dart analyze lib"; exit 1
fi

echo "  SKIP no dart / flutter SDK"
exit 77
