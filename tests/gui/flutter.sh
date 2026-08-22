#!/bin/sh
# flutter.sh -- GUI layer: the Flutter app (app/flutter).
#
# With a test/ dir and the Flutter SDK, run `flutter test`. Otherwise `dart
# analyze lib`, which catches FFI binding and widget errors without a device.
# With no Dart at all, SKIP.
#
# Exit 0 = passed, 1 = failed, 77 = SKIP.

app=$(cd "$(dirname "$0")/../../app/flutter" 2>/dev/null && pwd)
[ -n "$app" ] || { echo "  SKIP app/flutter not found"; exit 77; }
cd "$app" || { echo "  SKIP cannot enter app/flutter"; exit 77; }

if [ -d test ] && command -v flutter >/dev/null 2>&1; then
    # Resolve against the COMMITTED lock, and fail if it cannot be honoured.
    # `flutter test` resolves implicitly and rewrites pubspec.lock when a
    # transitive version has moved, which is a tracked file: on a release runner
    # that made the checkout dirty, so `git describe --dirty` stamped the
    # published binary "0.3.19-dirty" and the download was named after it. A
    # release artifact must be able to name the commit it came from.
    if ! flutter pub get --enforce-lockfile >/dev/null 2>&1; then
        echo "  FAIL pubspec.lock does not match what pub resolves"
        echo "       (run: flutter pub get, review the diff, commit it)"
        exit 1
    fi
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
