# Publishing the iPhone app

The runbook for the Apple side: enrolment, signing, TestFlight, the App Store.
It assumes what this repo already has, which is a working iOS build.
[Issue #1](https://github.com/Anode1/ais/issues/1) is the developer brief (how
the engine is wired, how to run it, and the acceptance list to run on a real
phone), and the `ios-build` job in `.github/workflows/flutter.yml` compiles the
app unsigned on a macOS runner and boots it on a simulator on every change to
`c/**`. Version numbers come from the git tag ([`VERSIONING.md`](VERSIONING.md)).

**No Mac is required.** Every step below is a web form, an `openssl` command on
Linux, or a step added to the CI job that already runs on macOS. The cost is
debugging: with no Mac there is no simulator, no `flutter run` and no
breakpoints, so each fix is a CI round trip of 10 to 20 minutes.

Order, and step 0 comes before the money:

    licence -> enrol -> certificate -> app record -> signed build -> TestFlight -> App Store

Four values are generated along the way and needed later. Write them down as they
appear: **Team ID**, **profile name**, **API Key ID**, **API Issuer ID**.

## 0. The licence, before anything else

**Resolved 2026-08-30**: the project is dual licensed, GPL-2.0-or-later or MIT
(`commercial/LICENSE-MIT`), and App Store binaries distribute under MIT, so the
exception below is not needed. The section stays as the reasoning.

AIS is GPL v2 or later. Apple's Licensed Application End User License Agreement
limits use to devices the buyer owns or controls, plus Family Sharing. Section 6
of the GPL forbids imposing further restrictions on the rights it grants, and
that clash is why VLC was pulled from the App Store in 2011, on a complaint from
one of its copyright holders. It takes a copyright holder objecting; nobody
enforces it unaided.

You are the only one: the single commit under a second account (`.gitignore`,
`.github/workflows/release.yml`) was the author's own work, so there is one
copyright holder. The standard exception would have been:

    As a special exception, the copyright holders of AIS give permission to
    distribute binaries built from this source through Apple's App Store and
    TestFlight, and to have those binaries governed by Apple's Licensed
    Application End User License Agreement, notwithstanding the
    further-restrictions clause in section 6 of the GNU General Public
    License version 2. This permission covers those distribution channels
    only and does not otherwise limit the rights the GPL grants.

`COPYING` is the verbatim GPL text and is not edited: the exception goes in its
own file next to it, named from `README.md`'s License section. This is a licence
change to a published project, so read it rather than paste it.

## 1. Enrol: $99 a year

The fastest route is the phone: install **Apple Developer** from the App Store,
go to Account, Enroll. Identity is verified with the device camera, which is why
this beats the web form at
[developer.apple.com/programs/enroll](https://developer.apple.com/programs/enroll).

- Entity type **Individual / Sole Proprietor**. It needs no D-U-N-S number and
  clears in a day or two, where an organisation takes weeks.
- **Your legal name becomes the seller name** on the listing. Without a company
  there is no other option; a trading name needs registration documents.
- The Apple ID needs two-factor authentication, and in practice it is the account
  holder forever: moving an app to a different account later is a support
  request, not a setting.
- $99 auto-renewing. A lapse pulls the app from the store and stops TestFlight
  builds from installing.

Then [developer.apple.com/account](https://developer.apple.com/account) >
Membership details, and record the **Team ID** (ten characters).

## 2. The distribution certificate, made on Linux

A certificate is a signed request. `openssl` makes the request, Apple's web form
signs it, and nothing here needs a keychain.

```sh
openssl genrsa -out ios_distribution.key 2048
openssl req -new -key ios_distribution.key -out ios_distribution.csr \
    -subj "/emailAddress=you@example.com/CN=Vasili Gavrilov/C=US"
```

[developer.apple.com/account/resources/certificates](https://developer.apple.com/account/resources/certificates)
> **+** > **Apple Distribution** > upload `ios_distribution.csr` > Continue >
Download. That gives `distribution.cer`. Pair it back with the private key:

```sh
openssl x509 -inform DER -in distribution.cer -out distribution.pem
openssl pkcs12 -export -legacy -inkey ios_distribution.key -in distribution.pem \
    -name "Apple Distribution" -out distribution.p12
```

`-legacy` is not optional. OpenSSL 3 defaults to an encryption that the macOS
`security import` on the runner cannot read, and the failure is a bare "MAC
verification failed" at import time, long after the mistake.

Unlike the Android keystore, **this key is replaceable**: Apple re-signs App
Store downloads with its own certificate, so losing yours means revoking it and
issuing another, not losing the ability to update the app. Still keep
`ios_distribution.key` and the `.p12` password off this machine and out of git.

## 3. The App ID and the provisioning profile

Same site, two more forms.

**Identifiers** > **+** > App IDs > App > Continue:

- Description `AIS`, Bundle ID **explicit**, `com.aisindex.ais`, matching Android
  and the value already in `app/flutter/ios/Runner.xcodeproj`.
- No capabilities to enable. The deep link is a URL scheme in `Info.plist`, local
  network access is a usage string, and speech runs through the plugin: none is a
  capability, and none needs Apple's approval. The one that would have, the
  multicast networking entitlement, does not apply: `c/sync.c` resolves a host
  you give it and discovers nothing by itself.

**Profiles** > **+** > Distribution > **App Store Connect** > Continue:

- App ID: the one just made. Certificate: the one from step 2. No devices are
  registered for an App Store profile.
- Name it `AIS App Store` and record that name; it goes into
  `ExportOptions.plist` verbatim in step 6.
- Download the `.mobileprovision`.

## 4. The app record, and the API key

[appstoreconnect.apple.com](https://appstoreconnect.apple.com) > Apps > **+** >
New App:

- Platform iOS, Name `AIS`, primary language, Bundle ID `com.aisindex.ais`,
  SKU `ais-ios`, Full access.
- The name must be free across the whole App Store. If `AIS` is taken, the
  listing name changes and the bundle id does not.
- Listing copy is in [`../public-text.txt`](../public-text.txt), written for Play
  and it transfers. Privacy policy URL: `PRIVACY.md` in this repo, the same one
  Play uses. Support URL: the repo's issues page.
- App Privacy: nothing is collected. No account, no analytics, no third-party
  SDK, and the index leaves the device only for a peer the user pairs with.
- **Decide the device family now.** The Xcode project carries
  `TARGETED_DEVICE_FAMILY = "1,2"`, iPhone and iPad, and an iPad-capable listing
  requires iPad screenshots. Either produce them or set the project to `"1"`.

Then Users and Access > **Integrations** > App Store Connect API > Team Keys >
**+**, access **App Manager**. Download the `.p8` (**once**; Apple never serves
it again) and record the **Key ID** and the **Issuer ID**. This is what lets CI
upload with no human at a Mac.

## 5. Export compliance

The app carries non-Apple cryptography: vendored Monocypher, XChaCha20-Poly1305,
for on-device secrets and for the sync payload. Every build sits in TestFlight
marked "Missing Compliance" and refuses to install until this is answered.

The source is public, which is the easy road: EAR 740.13(e) exempts publicly
available encryption source code, and claiming it takes one notification email to
`crypt@bis.doc.gov` and `enc@nsa.gov` naming the repository URL. Send it, keep
the sent copy, then answer the App Store Connect questionnaire on that basis.
Once answered for a version, add this to
`app/flutter/ios/Runner/Info.plist` so it stops being asked on every upload:

    <key>ITSAppUsesNonExemptEncryption</key>
    <true/>

That key is not in the file yet. This is export law rather than a build setting:
the questionnaire's own wording is what governs, and it is worth twenty minutes
of reading before clicking.

## 6. A signed build out of CI

Six secrets, under Settings > Secrets and variables > Actions, where the
`ANDROID_*` ones already live ([`ANDROID_RELEASE.md`](ANDROID_RELEASE.md)):

| Secret | From |
| --- | --- |
| `IOS_DIST_P12_BASE64` | `base64 -w0 distribution.p12` (step 2) |
| `IOS_DIST_P12_PASSWORD` | the export password from step 2 |
| `IOS_PROFILE_BASE64` | `base64 -w0 AIS_App_Store.mobileprovision` (step 3) |
| `APPSTORE_KEY_ID`, `APPSTORE_ISSUER_ID` | step 4 |
| `APPSTORE_KEY_P8_BASE64` | `base64 -w0 AuthKey_<KeyID>.p8` (step 4) |

The Team ID and the profile name are not secrets. They ship inside every signed
app, so they go in the repo.

Two files to commit. `app/flutter/ios/ExportOptions.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>method</key>            <string>app-store-connect</string>
  <key>teamID</key>            <string>YOUR_TEAM_ID</string>
  <key>signingStyle</key>      <string>manual</string>
  <key>uploadSymbols</key>     <true/>
  <key>provisioningProfiles</key>
  <dict><key>com.aisindex.ais</key><string>AIS App Store</string></dict>
</dict>
</plist>
```

And the Runner target's Release configuration in
`app/flutter/ios/Runner.xcodeproj/project.pbxproj`, which today says
`CODE_SIGN_STYLE = Automatic` and would send the archive looking for an Xcode
account that does not exist on a runner:

    CODE_SIGN_STYLE = Manual;
    DEVELOPMENT_TEAM = YOUR_TEAM_ID;
    CODE_SIGN_IDENTITY = "Apple Distribution";
    PROVISIONING_PROFILE_SPECIFIER = "AIS App Store";

Editing that file by hand is the point rather than the compromise: the settings
land in a diff instead of in Xcode state nobody can review, which is the reason
the engine's file list lives in `ais_engine.podspec`. The existing `ios-build`
job is unaffected, since `--no-codesign` never reaches signing.

A new job, gated on the certificate secret being present so forks and
unconfigured runs skip it, as the `android` job gates on the keystore:

```yaml
  ios-release:
    if: startsWith(github.ref, 'refs/tags/v')
    runs-on: macos-latest
    steps:
      # checkout with fetch-depth: 0, flutter-action, pub get, as in ios-build
      - name: signing material
        run: |
          security create-keychain -p "$RUNNER_KC" build.keychain
          security set-keychain-settings -lut 3600 build.keychain
          security unlock-keychain -p "$RUNNER_KC" build.keychain
          security list-keychains -d user -s build.keychain login.keychain
          echo "$P12" | base64 -d > /tmp/d.p12
          security import /tmp/d.p12 -k build.keychain -P "$P12_PASS" \
                   -T /usr/bin/codesign -T /usr/bin/security
          security set-key-partition-list -S apple-tool:,apple:,codesign: \
                   -s -k "$RUNNER_KC" build.keychain
          for d in ~/Library/MobileDevice/Provisioning\ Profiles \
                   ~/Library/Developer/Xcode/UserData/Provisioning\ Profiles; do
            mkdir -p "$d"
            echo "$PROFILE" | base64 -d > "$d/ais.mobileprovision"
          done
          mkdir -p ~/.appstoreconnect/private_keys
          echo "$KEY" | base64 -d > ~/.appstoreconnect/private_keys/AuthKey_$KEY_ID.p8
      - name: build
        run: |
          flutter build ipa --release $(sh tool/version.sh) \
                --export-options-plist=ios/ExportOptions.plist
      - name: upload to App Store Connect
        run: |
          xcrun altool --upload-app -t ios -f build/ios/ipa/*.ipa \
                --apiKey "$KEY_ID" --apiIssuer "$ISSUER_ID"
```

Both profile directories are written because Xcode 16 moved the location and
still reads the old one.

Two ways it goes wrong quietly:

- **Without the version flags**, `flutter build ipa` stamps the stale fallback in
  `pubspec.yaml`. `CFBundleVersion` has to rise on every upload or App Store
  Connect rejects the build; `tool/version.sh` derives it from the commit count.
- **A keychain left locked** signs nothing, and the error names the certificate
  rather than the lock.

## 7. TestFlight, onto the phone

The build reaches TestFlight 10 to 60 minutes after upload, marked "Missing
Compliance" until step 5 is answered.

A tester is invited by **Apple ID**, not by device: whoever installs signs into
TestFlight with the invited account. On a phone signed in as its owner, invite
the owner's Apple ID.

- Users and Access > **+** > invite that Apple ID, role Developer. That makes the
  person an internal tester, and internal builds skip Beta App Review, so the
  build installs as soon as processing finishes.
- TestFlight > Internal Testing > create a group > add the tester > select the
  build.
- They install **TestFlight** from the App Store and accept the emailed invite.
- External testers, up to 10,000, need Beta App Review on the first build. That
  is the route for anyone not on the account.
- **Builds expire after 90 days.** A test cycle that drags needs a re-upload.

Then run the acceptance list in issue #1 on the phone. Nothing before this point
exercises the camera QR scan, speech, sync over a real network, or force-quit
persistence, and the simulator in CI covers none of them.

## 8. The App Store

The same app record, its Distribution tab: pick the build, add screenshots, a
category (Productivity), an age rating, and review notes.

Say in the notes that sync needs a second device and the reviewer can skip it,
that there is no account to sign into, and that nothing reaches a server.
Reviewers reject what looks like it should have a login and does not more often
than they reject what they cannot exercise. App Store Connect's own page names
the screenshot sizes it wants; take them from the phone.

Review runs a day or two. A rejection arrives with a guideline number and a reply
box, and answering in the box is usually faster than resubmitting.

## Never automatable

Enrolment and identity verification, the licence decision in step 0, whether the
name `AIS` is free, the export-compliance answer, and the first review.
