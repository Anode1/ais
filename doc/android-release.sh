#!/bin/sh
# Android release runbook for the AIS native app (app/flutter).
# Runnable for the buildable steps; the store steps are MANUAL (marked below).
# Prereq once: release signing is set up (see app/flutter/android/key.properties.example).
# Order: test -> closed test (14 days) -> production. Do not skip the test stage.
set -eu

# ---- variables ----
APP=app/flutter                          # flutter project (run this from the repo root)
# Version lives in app/flutter/pubspec.yaml as  version: X.Y.Z+BUILD
# Bump it before building (BUILD = versionCode, must increase every Play upload).

# ---- 0. preflight ----
command -v flutter >/dev/null || { echo "flutter not on PATH"; exit 1; }
test -f "$APP/android/key.properties" || { echo "missing $APP/android/key.properties (copy from key.properties.example)"; exit 1; }
cd "$APP"
flutter pub get
flutter analyze

# ---- 1. build signed artifacts ----
flutter build apk --release        # build/app/outputs/flutter-apk/app-release.apk  (sideload / F-Droid / GitHub)
flutter build appbundle --release  # build/app/outputs/bundle/release/app-release.aab (Google Play upload)

# ---- 2. checksums (attach to the GitHub release notes) ----
sha256sum build/app/outputs/flutter-apk/app-release.apk
sha256sum build/app/outputs/bundle/release/app-release.aab

# ---- 3. TEST (do this BEFORE any production push) ----
# Install the release APK on a real device and exercise: recall, add, mic (allow
# the permission on first tap, speak, confirm text fills + recall runs), change store.
#   adb install -r build/app/outputs/flutter-apk/app-release.apk
# Optional wider testers: Firebase App Distribution, or Play Console internal testing.

# ---- 4. MANUAL: Google Play ----
# 4a. Play Console -> create app (once). Enable Play App Signing (Google holds the key).
# 4b. Internal testing track -> upload the .aab -> add testers by email -> they install.
# 4c. Closed testing track -> >= 12 testers for 14 days  (required for new personal
#     accounts before production is unlocked). Keep the track running the full 14 days.
# 4d. Fill: Privacy policy URL, Data safety form, Content rating, target-API statement.
# 4e. Promote the tested build to Production.

# ---- 5. MANUAL: F-Droid (the FOSS front door, parallel to Play) ----
# Submit metadata to fdroiddata; F-Droid BUILDS FROM SOURCE on their infra (no keys
# of ours). Requires the app + deps be FOSS (GPLv2 + Monocypher CC0/BSD: OK).
# Or self-host an fdroid repo and publish app-release.apk there.

# ---- 6. MANUAL: GitHub release ----
# Tag the repo, create a Release, attach app-release.apk + the sha256 from step 2.
# (Pairs with Obtainium for user auto-updates.)

echo "built apk + aab. Now: test (step 3) -> Play closed test 14d (4c) -> production."
