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
  `.aab`; the GitHub release artifact is an `.apk`, so this is a separate build.
- Package id: `com.aisindex.ais`.

## 1. Build the Play bundle (the one runnable step)

```sh
# build a release App Bundle for Play (not the CI's apk)
cd app/flutter
flutter build appbundle --release
# output: build/app/outputs/bundle/release/app-release.aab
```

For a real upload, sign it with your upload key (set `android/key.properties`
and the `signingConfigs`, the same keystore the CI release-signing step uses),
or let **Play App Signing** hold the app key while you keep only the upload key.

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
```
