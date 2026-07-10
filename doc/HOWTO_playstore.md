# HOWTO: publish AIS to the Google Play Store

The gate for a new personal developer account: run a closed test with **20
testers for 14 continuous days**, then apply for production. The catch most
people miss: Google only counts testers who install **through Play**, not people
you hand the APK to.

## What Google actually measures

Your testers do NOT sideload the GitHub APK. You add their Google-account emails
to a closed-testing list; each opens a private opt-in link and installs AIS from
the Play Store. Every opt-in and install is then recorded in your Play Console
automatically, because Google is the one delivering the app. The GitHub `.apk`
is invisible to this process and counts for nothing.

## Requirements

- A Play developer account ($25, one-time). **Personal** accounts created after
  late 2023 must pass the 20-tester / 14-day closed test before production.
  **Organization** accounts skip it but need D-U-N-S business verification.
- A signed **Android App Bundle (`.aab`)**, not an APK. New Play apps must ship
  `.aab`. Each tagged CI release attaches a correctly release-signed
  `ais-<tag>-android.aab` (built with `ais-release.jks` via the `ANDROID_*`
  secrets), next to the `.apk` -- that bundle is the simplest thing to upload.
- Package id: `com.aisindex.ais`.

## 1. Get a release-signed Play bundle

Two sources; the first is foolproof.

**A. Download the CI bundle (recommended).** Every tagged release attaches
`ais-<tag>-android.aab`, already signed with your upload key (`ais-release.jks`,
via the `ANDROID_*` secrets). Download it from the GitHub Releases page and go to
step 2.

**B. Build locally:**

```sh
cd app/flutter
flutter build appbundle --release
# output: build/app/outputs/bundle/release/app-release.aab
```

> **Warning -- a local release build is DEBUG-signed unless you set up the key.**
> When `android/key.properties` is absent, `android/app/build.gradle.kts` falls
> back to the debug keystore, so the command above silently emits a
> **debug-signed** bundle -- Play rejects it (*"signed with the wrong key"*: the
> debug cert, not your upload cert). To sign locally, copy
> `android/key.properties.example` to `android/key.properties`, pointing at
> `ais-release.jks` (alias `ais`) with the same passwords as the
> `ANDROID_STORE_PASSWORD` / `ANDROID_KEY_PASSWORD` secrets. Verify before
> uploading: `keytool -printcert -jarfile <the.aab> | grep SHA1` must show your
> upload cert, not the debug one. When unsure, use option A.

Play App Signing holds the app key that signs what users download; you only ever
sign uploads with this upload key.

## 2. Create the app (Play Console, manual)

- Create app: name `AIS`, app + free, complete the declarations.
- Store listing: short and full description, the icon (`icons/ais-512.png`), at
  least two phone screenshots (use `screenshots/android-*.png`), a feature graphic.
- Content-rating questionnaire, Data-safety form, a privacy-policy URL.

## 3. Closed testing: 20 testers, 14 days (manual)

- Testing > Closed testing: create a track, upload the `.aab` from step 1.
- Testers: add an email list (the 20 Google accounts) or a Google Group.
- Send each the opt-in URL; they accept and install AIS **from Play**.
- Keep at least 20 opted in for 14 straight days. If a tester opts out the count
  drops and the clock effectively pauses, so confirm they stay in (and opening
  the app a few times shows real engagement the production review likes).

## 4. Apply for production (manual)

- After 14 days with 20 maintained, the Console unlocks "apply for production".
- Answer the questionnaire (how you recruited testers, what feedback you got).
  Be truthful: obviously-fake testers get rejected. Review takes days to weeks.
- On approval, promote the build to the Production track and publish.

## Notes

- 20 is a hard floor. Wife plus a few friends is usually not enough; plan to
  recruit roughly 15 to 20 real people, each on their own Google account and device.
- This is the Android twin of TestFlight for iOS: private distribution plus a
  real testing window before the public store.
- Policies shift. The Console's own production-access checklist is the source of
  truth; the above is accurate as of mid-2026.

## Appendix: automate uploads later (after the first manual release)

The Play Developer API can push the `.aab` from CI to a track, so you stop
uploading by hand. It works only once the app already exists in the Console and
has had at least one build uploaded manually: the API updates an existing app,
it cannot create the first one or pass the 20-tester gate for you.

One-time setup (manual, in the Console and Google Cloud):

- Play Console > Setup > API access: link a Google Cloud project, create a
  service account, and grant it permission to release to your tracks.
- In Google Cloud, create and download a JSON key for that service account.
- Add the JSON as a CI secret, e.g. `PLAY_SERVICE_ACCOUNT_JSON`.

Then push from CI with one command (fastlane `supply`):

```sh
# upload to the internal track; promote in the Console, or pass
# --track production once you trust the pipeline
fastlane supply --package_name com.aisindex.ais \
  --aab dist/ais-<tag>-android.aab --track internal \
  --json_key "$PLAY_SERVICE_ACCOUNT_JSON_FILE"
```

Gate that step on the secret being present (the way the keystore-signing step
is), so forks and unconfigured runs skip it. The Gradle Play Publisher plugin
(`com.github.triplet.play`, run with `./gradlew publishReleaseBundle`) is the
same API configured in Gradle instead, if you prefer not to add Ruby.

Still not automatable: account ownership and ID verification, recruiting and
keeping the 20 testers, and the first production review.
