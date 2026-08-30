# iOS release TODO

The order to do it in. Each step's detail is in [IOS_RELEASE.md](IOS_RELEASE.md);
this is only the checklist. Record as they appear: Team ID, profile name,
API Key ID, API Issuer ID.

## 0. Licence (before paying Apple)

- [x] Dual licence added (2026-08-30): MIT in `commercial/LICENSE-MIT` beside
      the GPL. App Store binaries distribute under MIT, so no GPL exception is
      needed.
- [x] No consent to collect: the one commit under a second account
      (`.gitignore`, `release.yml`) was the author's own work, so there is a
      single copyright holder.

## 1. Enrol as a developer ($99/yr)

- [ ] On the iPhone: install the **Apple Developer** app, Account > Enroll,
      entity **Individual / Sole Proprietor**. Identity check uses the phone
      camera. Clears in a day or two. Legal name becomes the seller name.
- [ ] Go <https://developer.apple.com/account> > Membership details, record the
      **Team ID**.

## 2. Distribution certificate (on Linux, no Mac)

- [ ] `openssl genrsa` + `openssl req` (commands in IOS_RELEASE.md step 2).
- [ ] Go <https://developer.apple.com/account/resources/certificates> > + >
      **Apple Distribution** > upload the `.csr` > download `distribution.cer`.
- [ ] Make `distribution.p12` (`openssl pkcs12 -export -legacy`). Keep the key
      and password out of git.

## 3. App ID and profile

- [ ] Go <https://developer.apple.com/account/resources/identifiers> > + >
      App IDs: explicit `com.aisindex.ais`, no capabilities.
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
- [ ] Users and Access > Integrations > App Store Connect API > Team Keys > +,
      access **App Manager**. Download the `.p8` (served once), record
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
- [ ] Commit `app/flutter/ios/ExportOptions.plist` (template in step 6, real
      Team ID).
- [ ] In `project.pbxproj` Release config: `CODE_SIGN_STYLE = Manual`, team,
      identity, profile specifier.
- [ ] Add the `ios-release` job to `.github/workflows/flutter.yml`, gated on a
      tag and on the certificate secret. `altool --upload-app` is deprecated
      for uploads; if it refuses, switch to `destination: upload` in
      `ExportOptions.plist` with the `-authenticationKey*` flags, or
      `iTMSTransporter`.
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
