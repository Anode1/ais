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
- [ ] Go <https://developer.apple.com/account/resources/certificates> > + >
      **Apple Distribution** > upload the `.csr` > download `distribution.cer`.
- [ ] Make `distribution.p12` (`openssl pkcs12 -export -legacy`). Keep the key
      and password out of git.

## 3. App ID and profile

- [x] App ID explicit `com.aisindex.ais`, no capabilities, registered from the
      iPhone (2026-10-08).
- [ ] Profiles > + > Distribution > **App Store Connect**: that App ID, that
      certificate, name it `AIS App Store`, download the `.mobileprovision`.

## 4. App record and API key

- [ ] Go <https://appstoreconnect.apple.com> > Apps > + > New App: iOS, bundle
      `com.aisindex.ais`, SKU `ais-ios`. Name `AIS` is likely taken (marine
      ship trackers); fall back to `AIS Index` or similar, bundle id unchanged.
- [ ] Listing from `doc/public-text.txt`; privacy policy URL = `PRIVACY.md`;
      support URL = the issues page. App Privacy: nothing collected.
- [ ] Decide device family: keep iPad (`TARGETED_DEVICE_FAMILY = "1,2"`) and
      make iPad screenshots, or set `"1"` in `project.pbxproj`.
- [ ] Users and Access > Integrations > App Store Connect API > **Request
      Access** (account holder only, once), then Team Keys > +, access
      **App Manager**. Download the `.p8` (served once), record
      **Key ID** and **Issuer ID**.

## 5. Export compliance

- [ ] Email the repo URL to `crypt@bis.doc.gov` and `enc@nsa.gov`
      (EAR 740.13(e), public source). Keep the sent copy.
- [ ] Answer the App Store Connect questionnaire on that basis, then set
      `ITSAppUsesNonExemptEncryption` in `Info.plist`. Read the questionnaire
      before choosing the value: exempt-only means `false`, and `false` is what
      stops the per-build question.

## 6. Signed build from CI

- [ ] Add the six repo secrets (table in IOS_RELEASE.md step 6):
      `IOS_DIST_P12_BASE64`, `IOS_DIST_P12_PASSWORD`, `IOS_PROFILE_BASE64`,
      `APPSTORE_KEY_ID`, `APPSTORE_ISSUER_ID`, `APPSTORE_KEY_P8_BASE64`.
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
