#!/bin/sh
# version.sh -- the app version, derived from the git tag instead of hand-edited.
#
# Prints the flags `flutter build` needs, so the tag is the ONE source of truth for
# the store metadata AND the About screen:
#
# # release builds (run from app/flutter)
# flutter build appbundle --release $(sh tool/version.sh)
# flutter build ipa --release $(sh tool/version.sh)
#
# # see what it resolved to
# sh tool/version.sh
#
# --build-name/--build-number set the platform metadata (versionName/versionCode,
# CFBundleShortVersionString/CFBundleVersion) that Play and the App Store read. The
# matching --dart-define pair is what lib/version.dart shows in About. Both pairs
# carry the same two values, so the screen cannot drift from the listing.
#
# name    the nearest vX.Y.Z tag with the v stripped: Android and iOS both want a
#         plain dotted number, so no -dirty / -gSHA suffix can be carried here.
# number  the commit count on HEAD. Monotonically increasing on a single trunk,
#         which is exactly what Play demands of versionCode; it only ever goes up.
#         (It jumps past pubspec's hand-maintained +9 on the first use, which is
#         fine -- versionCode may skip, it may not go back.)
#
# An untagged tree (a shallow CI clone fetched without tags) falls back to 0.0.0,
# which is visibly wrong on the About screen rather than silently wrong.
set -e
cd "$(dirname "$0")/.."

name=$(git describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null || echo v0.0.0)
name=${name#v}
number=$(git rev-list --count HEAD 2>/dev/null || echo 0)

printf -- '--build-name=%s --build-number=%s --dart-define=APP_VERSION=%s --dart-define=APP_BUILD=%s\n' \
    "$name" "$number" "$name" "$number"
