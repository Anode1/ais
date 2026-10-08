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
- [x] Screenshots: `scripts/appstore-shots.sh` makes the four 6.9-inch
      frames (1320x2868) in `fastlane/metadata/ios/en-US/images/phoneScreenshots/`
      from the Android captures with the status and gesture bars cropped off
      (2026-10-08). Review may still read them as not-iOS; captures from the
      TestFlight install on Marina's iPhone would replace them, same script.
- [ ] 1.0: upload the four screenshots, attach build 526, Submit for Review.
- [x] Build 526 Ready to Submit in TestFlight (2026-10-08), auto-distributed
      to the `Family` group.
- [x] Device family: iPhone only (`TARGETED_DEVICE_FAMILY = "1"`,
      2026-10-08). iPad can be added in a later version; it could not have
      been removed.
- [x] App Store Connect API access granted and team key `ais ci` (App
      Manager) made 2026-10-08: Key ID `R2T7YDFCH6`, `.p8` in
      `~/ais-signing/ios/`, Issuer ID `92c167f9-6945-4d98-9efc-ff68cf777d23`.

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

## 7. First device tests (build 526 on Marina's iPhone, 2026-10-08)

- [x] Scan-to-join: the camera opened the app from the Android's QR and nothing
      followed. The scene delegate built the `ais/deeplink` channel from the
      window's root view controller at connect time, which is not reliably the
      Flutter controller yet, so no handler was installed and Dart's
      getInitialLink failed into its silent catch. Fixed: the channel is made in
      AppDelegate from the engine bridge's messenger, the scene only stores and
      forwards (`SceneDelegate.swift`). Unverified until the next TestFlight build.
- [ ] Host on the iPhone, join from Android: the join never reached the iPhone
      (its SYNs were dropped; the iPhone waited out its five minutes). Same
      Wi-Fi. Likeliest cause: the app took the first private address on any
      interface, and with cellular on the carrier's 10.x address can list
      before Wi-Fi. Fixed: the Wi-Fi interface (en0, wlan0) is tried first.
      Unverified; if it recurs, compare Settings > Wi-Fi > (i) with the QR,
      and consider router isolation between the two phones.

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
- [ ] Tag a release, watch the upload reach App Store Connect.

## 7. TestFlight

- [ ] Users and Access > + > invite your own Apple ID and the second
      tester's, role Developer (internal testers, no Beta App Review); two
      phones widen the hardware coverage and give sync a real second device.
- [ ] TestFlight > Internal Testing > group > tester > build.
- [ ] Install **TestFlight** on the phone, accept the invite, install AIS.
- [ ] Run the acceptance list from issue #1 on the phone: QR join, speech
      (first real test of it), sync over a real network, force-quit
      persistence. Builds expire in 90 days.

## 8. App Store

- [ ] Distribution tab: pick the build, screenshots from the phone, category
      Productivity, age rating.
- [ ] Review notes: no account to sign into, nothing reaches a server, sync
      needs a second device and the reviewer can skip it.
- [ ] Submit. A rejection comes with a guideline number and a reply box;
      answer there first.
