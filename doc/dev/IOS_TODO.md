# iOS release TODO

The order to do it in. Each step's detail is in [IOS_RELEASE.md](IOS_RELEASE.md);
this is only the checklist. Record as they appear: Team ID, profile name,
API Key ID, API Issuer ID.

## 0. Licence (before paying Apple)

- [x] Dual licence added (2026-08-30): MIT in `LICENSE-MIT` beside
      the GPL. App Store binaries distribute under MIT, so no GPL exception is
      needed.
- [x] No consent to collect: the one commit under a second account
      (`.gitignore`, `release.yml`) was the author's own work, so there is a
      single copyright holder.

## 1. Enrol as a developer ($99/yr)

- [x] On the iPhone: install the **Apple Developer** app, Account > Enroll,
      entity **Individual / Sole Proprietor**. Identity check uses the phone
      camera. Legal name becomes the seller name. Cleared 2026-10-02, five days
      after payment.
- [x] Team ID `3T7N3KADW5`, read off the App ID registration form's "App ID
      Prefix" (2026-10-08). The Developer app and the account page's
      Membership details card do not show it on a phone.

## 2. Distribution certificate (on Linux, no Mac)

Steps 2 and 3 need the account holder's own sign-in (IOS_RELEASE.md step 1).

- [x] `openssl genrsa` + `openssl req` (commands in IOS_RELEASE.md step 2),
      2026-10-02.
- [x] Apple Distribution certificate issued 2026-10-08, valid to 2027-10-08;
      `distribution.p12` made with `-legacy`, password beside it in
      `~/ais-signing/ios/p12_password.txt`.

## 3. App ID and profile

- [x] App ID explicit `com.aisindex.ais`, no capabilities, registered from the
      iPhone (2026-10-08).
- [x] Profile `AIS App Store` (App Store Connect, that App ID, that
      certificate), downloaded 2026-10-08, expires with the certificate.

## 4. App record and API key

- [x] App record made 2026-10-08: iOS, bundle `com.aisindex.ais`, SKU
      `ais-ios`. `AIS` was taken; the store name is **AIS Index**. The bundle
      id and the name under the icon stay `AIS`.
- [x] 2026-10-08: category Productivity/Utilities, age rating 4+, content
      rights none, free in all countries, App Privacy "no data collected"
      published, privacy policy URL, 1.0 text (description from the fastlane
      folder, keywords, support and marketing URLs, manual release),
      TestFlight internal group `Family` with Marina as tester. Vas was not
      invited: he has no working Apple ID; CI and the API key need none.
- [x] First build uploaded by CI from the v0.3.30 tag (0.3.30, build 526,
      2026-10-08). altool warned: deployment target iOS 13.0; from April 2027
      uploads need 15.0 or later.
- [x] Screenshots: `scripts/appstore-shots.sh` resizes the four captures
      to 1206x2622, unframed, for the "Dynamic Island (medium)" slot (the only
      iPhone slot App Store Connect offers), into `fastlane/metadata/ios/en-US/images/phoneScreenshots/`
      from `screenshots/iphone_*.png`, Marina's captures on the 0.3.33 build
      (2026-10-08): timeline, add, search; encrypt retaken 2026-10-09 with
      the eye on both passphrase fields.
- [ ] 1.0: rename the version to the build's, upload the four screenshots,
      attach the build, Submit for Review.
- [x] Build 526 Ready to Submit in TestFlight (2026-10-08), auto-distributed
      to the `Family` group.
- [x] Device family: iPhone only (`TARGETED_DEVICE_FAMILY = "1"`,
      2026-10-08). iPad can be added in a later version; it could not have
      been removed.
- [x] App Store Connect API access granted and team key `ais ci` (App
      Manager) made 2026-10-08. The Key ID, Issuer ID and `.p8` are in
      `~/ais-signing/ios/` (off git) and in the repository secrets.

## 5. Export compliance

- [x] Notification emailed to `crypt@bis.doc.gov` and `enc@nsa.gov` on
      2026-10-08 (EAR 740.13(e), public source); text in
      `~/ais-signing/ios/bis-notification.txt`, sent copy in Vas's mailbox.
- [x] Questionnaire answered on build 526 (2026-10-08): standard algorithms
      in addition to Apple's; not distributed in France (that needs an ANSSI
      declaration first). Apple records that as exempt, so
      `ITSAppUsesNonExemptEncryption` is `false` in `Info.plist` and later
      uploads inherit the answer; `true` got v0.3.31's upload refused for a
      missing compliance code.
- [ ] France: file the ANSSI declaration, then answer Yes on a later build.

## 6. Signed build from CI

- [x] The six repo secrets set 2026-10-08 by
      `~/ais-signing/ios/set-secrets.sh` (`gh secret set`, needs the real
      Issuer ID as its argument).
- [x] `app/flutter/ios/ExportOptions.plist` committed (2026-09-01); real
      Team ID in since 2026-10-08.
- [x] `project.pbxproj` Release config: `CODE_SIGN_STYLE = Manual`, identity,
      profile specifier (2026-09-01), `DEVELOPMENT_TEAM` (2026-10-08).
- [x] `ios-release` job in `.github/workflows/flutter.yml` (2026-09-01), gated
      on a tag and on the certificate secret. `altool --upload-app` is
      deprecated for uploads; if it refuses, switch to `destination: upload` in
      `ExportOptions.plist` with the `-authenticationKey*` flags, or
      `iTMSTransporter`.
- [x] Privacy manifest for the engine's `stat` calls
      (`app/flutter/ios/PrivacyInfo.xcprivacy`, bundled by `ais_engine.podspec`,
      2026-10-02); the `ios-build` job asserts it is in the app.
- [x] Tag a release, watch the upload reach App Store Connect (v0.3.30,
      build 526, section 4).

## 7. TestFlight

- [x] Internal group `Family` with Marina as tester, builds auto-distributed
      (section 4).
- [x] **TestFlight** installed on Marina's iPhone, AIS installed from it.
- [ ] Run the acceptance list from issue #1 on the phone: QR join, speech
      (first real test of it), sync over a real network, force-quit
      persistence. Builds expire in 90 days.

## 8. First device tests (build 526 on Marina's iPhone, 2026-10-08)

A sync between the iPhone (0.3.34) and an Android phone (0.3.29) succeeded on
2026-10-09; which items below it closes awaits the direction it ran in.

- [x] Scan-to-join: the camera opened the app from the Android's QR and nothing
      followed. The scene delegate built the `ais/deeplink` channel from the
      window's root view controller at connect time, which is not reliably the
      Flutter controller yet, so no handler was installed and Dart's
      getInitialLink failed into its silent catch. Fixed: the channel is made in
      AppDelegate from the engine bridge's messenger, the scene only stores and
      forwards (`SceneDelegate.swift`). Shipped in 0.3.32; unverified.
- [ ] Host on the iPhone, join from Android: the join never reached the iPhone
      (its SYNs were dropped; the iPhone waited out its five minutes). Same
      Wi-Fi. Likeliest cause: the app took the first private address on any
      interface, and with cellular on the carrier's 10.x address can list
      before Wi-Fi. Fixed: the Wi-Fi interface (en0, wlan0) is tried first.
      Unverified; if it recurs, compare Settings > Wi-Fi > (i) with the QR,
      and consider router isolation between the two phones.
- [ ] Review of the scan fix found the held link was still pushed before Dart
      ran; a cold-start scan reached the app only through Flutter's own
      deep-link fallback, about 3 s late. Now the link waits for Dart's first
      ask and that fallback is off. To confirm: force-quit AIS, scan the
      Android's code, tap the banner: the filled-in Join must appear at once.
- [ ] The first join from the iPhone triggers the "allow local network
      access" alert, which can use up the join's 10 s; the Join dialog comes
      back once with a note to tap Sync again. Confirm it does, and that the
      second attempt connects.
- [ ] Speech (0.3.34, 2026-10-09): each phrase arrived appended to the earlier
      ones, even with the field cleared between ("1 3", then "Movies", gave
      "1 3 Movies"). iOS keeps one recognition session open until it is
      stopped and reports the transcript since it began; the app never
      stopped it. Fixed: a 3 s pause or 30 s in all ends the session, a tap on
      the live mic stops it, and the icon shows which state it is in.
      Unverified until the next TestFlight build.
- [ ] Speech, both platforms (Android, 2026-10-09): the phrase landed in the
      field and the search ran, but the body stayed on the timeline, so the
      Recent list showed unfiltered. The speech path called `_recall()`, which
      fills the results without switching the view; typing goes through
      `_recallLive()`. Fixed: speech goes through `_recallLive()` too.
      Unverified on a device.
- [x] The Sync sheet offered "Set a sync folder" on iOS, where
      file_selector_ios has no directory picker; the tap did nothing. The
      folder group is now hidden on iOS (2026-10-09).

## 9. App Store

- [ ] Distribution tab: attach the build (screenshots, category and age
      rating are done, section 4).
- [ ] Review notes: no account to sign into, nothing reaches a server, sync
      needs a second device and the reviewer can skip it.
- [ ] Submit. A rejection comes with a guideline number and a reply box;
      answer there first.
