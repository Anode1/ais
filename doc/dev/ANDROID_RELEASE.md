# Publishing the Android app

The runbook for getting a build onto a phone, into Play, and onto F-Droid. The
version numbers it uses come from the git tag; the release procedure that
produces the tag is in [`VERSIONING.md`](VERSIONING.md), and the signing keys
are in [`SIGNING.md`](SIGNING.md).

Order, and the test stage is not optional:

    build -> test on a real device -> Play closed test (14 days) -> production

## 1. Get a release-signed bundle

**A. Take the one CI built (recommended).** Every tag attaches
`ais-<tag>-android.apk` (sideload, F-Droid, GitHub) and `ais-<tag>-android.aab`
(the Play upload format), each with a `.sha256`, signed with the upload key from
the `ANDROID_*` secrets. Download and go to step 2.

**B. Build it locally:**

```sh
cd app/flutter
flutter build appbundle --release $(sh tool/version.sh)
# output: build/app/outputs/bundle/release/app-release.aab
```

Two ways that goes wrong, both silent:

- **Without the version flags** the build takes pubspec's fallback versionCode,
  which is far below anything already uploaded, and Play rejects it or accepts it
  into a track you did not mean. `tool/version.sh` derives both numbers from the
  tag; see `VERSIONING.md`.
- **Without `android/key.properties`** the release build falls back to the debug
  keystore, and Play rejects the bundle as signed with the wrong key. Copy
  `android/key.properties.example` and point it at your keystore. Verify before
  uploading: `keytool -printcert -jarfile <the.aab> | grep SHA1` must show the
  upload cert.

Play App Signing holds the key that signs what users download; the upload key
only signs what you hand to Play.

## 2. Test on a real device

```sh
adb install -r build/app/outputs/flutter-apk/app-release.apk
```

Exercise recall, add, switching the store, and the mic. Device setup and the mic
permission flow are in [`../android-install.md`](../android-install.md). For
wider testers before Play, Firebase App Distribution or Play internal testing.

The CI android job fails if `lib/arm64-v8a/libais.so` is missing from the apk,
which is what a native build that dropped the engine looks like. A local build
has no such guard: check the apk if you built one by hand.

## 3. Create the app in the Play Console

- Create app: name `AIS`, package `com.aisindex.ais`, free.
- Store listing text, the icon, the feature graphic and which screenshots to use
  are in [`../store-listing.txt`](../store-listing.txt), ready to paste.
- Content rating, Data safety, target-API statement, and the privacy-policy URL
  (`PRIVACY.md` in this repo).

An account costs $25 once. A **personal** account created after late 2023 must
pass the closed test below before production unlocks; an **organization** account
skips it but needs D-U-N-S verification.

## 4. Closed testing: 20 testers, 14 continuous days

What Google counts is installs **through Play**, so the GitHub `.apk` is
invisible to this process and counts for nothing.

- Testing > Closed testing: create a track, upload the `.aab`.
- Add the testers' Google-account emails (or a Google Group) and send them the
  opt-in link; each accepts and installs AIS from Play.
- Keep at least 20 opted in for 14 straight days. An opt-out drops the count and
  effectively pauses the clock, and opening the app a few times is the engagement
  the production review looks for.

20 is a hard floor: plan on recruiting 15 to 20 real people, each on their own
account and device. This is the Android twin of TestFlight.

## 5. Apply for production

The Console unlocks the application after the 14 days. Answer the questionnaire
truthfully, including how you recruited the testers; obviously fake testers get
rejected, and review takes days to weeks. On approval, promote the build to
Production. The Console's own checklist is the source of truth if any of this has
moved.

## 6. F-Droid, in parallel

F-Droid builds **from source** on their own infrastructure, holding no key of
ours, so it needs the app and its dependencies to be FOSS (GPLv2 plus Monocypher
under CC0/BSD: fine). Submit the metadata recipe to `fdroiddata`, or self-host an
F-Droid repo and publish the `.apk` there. This is the free-software front door
and it is independent of Play.

## Appendix: automate the upload later

The Play Developer API can push the `.aab` from CI once the app exists in the
Console and has had one build uploaded by hand: the API updates an app, it cannot
create the first one or pass the tester gate.

- Play Console > Setup > API access: link a Google Cloud project, create a
  service account, grant it release permission on your tracks.
- Download its JSON key and add it as a CI secret, e.g.
  `PLAY_SERVICE_ACCOUNT_JSON`.

```sh
fastlane supply --package_name com.aisindex.ais \
  --aab dist/ais-<tag>-android.aab --track internal \
  --json_key "$PLAY_SERVICE_ACCOUNT_JSON_FILE"
```

Gate that step on the secret being present, as the keystore step is, so forks and
unconfigured runs skip it. The Gradle Play Publisher plugin
(`com.github.triplet.play`) is the same API configured in Gradle instead.

Never automatable: account ownership and identity verification, recruiting and
keeping the 20 testers, and the first production review.
