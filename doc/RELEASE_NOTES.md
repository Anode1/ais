# Release notes

What changed in each release, newest first. Tags build the release
(`.github/workflows/release.yml`), and the entry here for that tag becomes the
release body on GitHub.

Before tagging, run `scripts/release-notes.sh vX.Y.Z`: it prepends the commit
subjects since the previous tag, then edit them down to what a user needs to
know. The old entries below the two newest are the raw subjects.

## v0.3.27 (2026-09-06)

- Android: Sync > Set a sync folder works with a folder another app shares (Syncthing, a cloud drive). The app keeps a private mirror of the folder for the engine and writes its own bundle back; a folder it cannot use is never remembered, and a folder that turns up empty is refused with Sync anyway offered.
- Web GUI: the result count includes value hits; an unmatched tag prefill gets focus; a 404 carries the hardening headers.
- Docs: every sync channel in one table (doc/SYNC.md); running the Linux build under WSL on Windows, and hosting a sync from there; the Host code waits five minutes.
- iOS: the CI signing job is in place with placeholders until enrolment. Nothing user-visible.

## v0.3.26 (2026-08-31)

- Android: the index can survive an uninstall. Sync > Keep a copy in a folder refreshes a copy after every change; Restore from a folder brings it back on the empty start screen.
- Android: text and links shared from other apps land in AIS.
- Search: tag matches rank before value matches; --find is case-insensitive; recall is near-linear on large keys.
- Tag autocomplete in the app and both web pages; passphrase confirm everywhere a secret is saved.
- Import: browser bookmarks HTML (--import-bookmarks) and a Google Keep takeout (--import-keep).
- Server and CLI hardening: Host header check, security headers, port and -f validation, exit codes (no match exits 1), tty hints.
- Licence: new code dual GPL v2+ / MIT.
- Tests: two macOS-only fixes (pty slave held open, signal.h).

## v0.3.25 (2026-08-25)

- add: a save of text already in the index says it merged (one entry, dated today) instead of Saved, in the app and both web pages

## v0.3.24 (2026-08-25)

- edit: a note's text is editable in every front end, multi-line included; the editor starts from the full text, an aisdoc row names its own blob, and a binary body stays view-only

## v0.3.23 (2026-08-24)

- sync: a hidden join says how it went, the pairing code is wiped once spent and the host waits 300s for a scan; the theme choice survives a restart

## v0.3.22 (2026-08-24)

- dedupe-docs: the caveat says why each device must run it, and what a copy coming back from one that has not looks like; the tomb hash is over the value, as the code has it
- dedupe-docs: the pre-0.3.20 clash copies (X, X-1, X-1-1 over one body) are listed beside the record kept and deleted on confirmation; a different body, a tagged name, an encrypted document and a multi-link record are left alone

## v0.3.21 (2026-08-23)

- the --set dup message names both cases on the app and the web too, as the CLI does
- main: the --set dup message covers the same-record link case it can name
- ais.h: the add contract states the own-value no-op, the set paragraph re-flowed; --add's message names only the case that reaches it
- edit: one value cannot sit on two lines of one record (--set refuses, --add no-ops, an E| skips; the pair exported as two A| and the peer collapsed them), the web -3 message points at the pages' own clean up, the doubled-limit prose moved to limitations.txt
- docs: PRIVACY.md tells the edit-fingerprint story, the edit log's growth is a stated limit, the off contract matches the code, the hash basis is stated in BNF, USING.txt gains Edit value
- edit: an arriving self-edit is a no-op (it shredded a document's blob), the purge writes no identity fact, every surface names why a --set was refused, and -f into a missing directory says so
- docs: the man page gains --set and drops the A|/D| round-trip claim, BNF.txt learns E|, T|, C| and the edits file, the pre-release caveat leaves SYNC.md, the version now lives in five files
- edit: the panel's blockers fixed; a merged link keeps its wire time, an unapplied E| is still recorded, a local edit cannot lower the survival stamp, forget-deleted compacts the edit log instead of dropping it, import loads that log once, a spool failure and an overlong line lose neither records nor order
- edit: an in-place value edit travels as E| and is applied in place on every device; the D| retirement lost a multi-value record and the peer's own changes
- import: a spool that fails falls back to direct puts, a hash hit is confirmed against the store line, a legacy line keyed A still flushes; set shifts off instead of dropping it
- ais.h: the set contract said the edit was local only; it is not, and the docs that still called import quadratic
- import: a run of adds resolves its values in one store pass (20k lines 13 s to 1.8 s); a raised record's key attach now runs at its true time, not an empty one
- flutter ci: read the version stamp before the build dirties the checkout; the ios check expected a -dirty the binary never had
- set: the replaced value is retired on the wire, so an in-place edit reaches every device; the app edits in place again
- gitattributes: legacy/ is an archive, so git stops renormalising it (every checkout looked dirty)

## v0.3.20 (2026-08-22)

- release: the version reaches the compiler, not just the directory name; drop the probe
- mesh test: compare keys as bytes; macOS awk works in characters and the check lied
- shard by a whole character: half a UTF-8 sequence is not a filename APFS accepts
- temp: a macOS probe for the Cyrillic key failure
- keys: encode by ASCII rule, not by locale; macOS mangled every Cyrillic key
- mesh test: BSD sed needs an argument to -i, so the legacy clash was never built on macOS
- app: an edit on a device that syncs is a delete and a fresh save, so it reaches the others
- add sheet: what you are saving first, its tags under it; the PWA already had it that way
- play: store-ready screenshots; the captures have alpha and a 2.22 ratio, which Play refuses
- export only the documents a record points at; keep the offset index alive after a top-id compaction
- flutter ci: the desktop layer builds and launches; it cannot see, so it reports
- flutter ci: the app rendered solid black; give it a software rasteriser
- flutter ci: keep the ui harness work dir, or its screenshots die with it
- release: stamp artifacts from the tag; the suite could dirty the tree and did
- flutter ci: upload the screenshots from where the harness actually writes them
- tests: an index from the last release, opened by this one, and the mixed mesh a rollout makes
- uitest: run.sh is bash, and sh made the layer fail the moment it first ran
- delete and value-edit dispose of the payload in the engine, once, and only when they happen
- flutter ci: run the desktop UI layer, which has never gated anything here
- app: say which device does what when pairing, offer Copy when a link will not open
- next_id: a cache below an id the store holds would reissue it
- sync: one big document no longer blocks the rest, and a folder that yielded nothing says so
- doc blobs: a name is unique at birth, and one import policy keeps both bodies
- compact: the tree being deleted leaves recovery's vocabulary first, or a kill restores the remnant
- flutter: an armed delete now settles on a timer and on leaving the screen, not on a snackbar animation
- flutter: the empty view's own Add stood beside the fab, the third surface the web pair already fixed
- android: declare the http(s) VIEW query, or package visibility hides every browser
- delete: a compacted tombstone is consulted by hash, so a stale peer cannot push the record back
- tests: a four-device mesh, and sync.sh could scrape the previous leg's token
- release doc: what to do when the tagged build fails, and the SDK level is not ours to state
- doc dev index: say where iOS is documented, since it is not here
- doc: the Windows port was described across four files; now one
- doc: the Windows status was stated twice per file; one door to the dev notes
- doc: two files named SYNC.md, and two files of paste-ready copy
- doc: front-end docs stated the same map four times, two of them stale
- doc: the test layers were described in four places; each now has one home
- doc: one Android release runbook; the three copies disagreed on versionCode
- doc: write down the release procedure, which lived only in practice
- web: one Add on an empty view where :has() is missing; the PWA first paint says Loading
- ios: the podspec named GPL-3.0; the tree is GPL-2.0-or-later
- ios: the app runs on a simulator in CI, so say that instead of what was missing
- ios ci: launch it on a simulator; a build cannot prove the FFI resolves
- ios: stamp the engine version from the tag, and hold the build to it
- ios: the engine ships as a framework in the bundle, not linked into the binary
- ios: leave the web server out; its only caller is the CLI and system() is unavailable
- ios: the project never knew about Pods, so PODS_ROOT was empty at build time
- ios: wire the engine into the app with a podspec, and let CI prove it compiles
- ios: the work lives in issue #1; drop the doc that duplicated it
- pwa: the empty views offer Add and hide the fab, as the embedded page does
- gui doc: reference shots of both web pages side by side, so drift shows
- serve: the header shares the list's 720px measure; an empty state's own Add hides the fab
- ios doc: shipping the app is the whole subject; the embedder rules move into embed.h
- ios doc: state what the work is, not who does it or how to feel about it
- ios: one file for an iOS developer; the scaffold TODO merged into IOS_NATIVE.md

## v0.3.19 (2026-08-20)

- release ci: pin flutter to the version the tests run, not floating stable
- flutter: Clean up in the menu; the phone had no way to compact and no CLI to fall back on
- sync: a mistyped token no longer ends the host session
- delete: a document blob goes with its record; a file you only pointed at never does
- pwa: the sync sheet the embedded page has had all along (host, join, file, folder)
- win32: the readme still called the Search button Recall
- screenshots: recapture the four Android shots from the redesigned app
- flutter: dense two-zone rows, chips for any-tag and date range, library path off the home screen
- serve: align the page with the app's Material palette, row anatomy and touch targets
- serve: style the empty-state Add button outside .actions too; recall rows show their tags (meta=1)
- flutter: dialogs own their fields via _OwnedFields; disposing at pop crashed every Save and Cancel
- off: a healed tail shifted the new slot by a byte; timeline scans stale slots instead of dropping
- Seventeen headings had settled into "Why X, and why it is Y"
- fixing comments
- fixing comments
- ais_tags counts live records; the header promised the opposite
- fixing comments

## v0.3.18 (2026-08-10)

- tests: pin the -e -v - overlong-secret refusal and its 1023-byte boundary
- makefile: the helpdoc recipe had split the hooks target, taking its echo
- gitignore: the ad-hoc Ada bench binaries from the -O3 placement measurement
- -e -v -: refuse a piped secret longer than the buffer instead of encrypting it truncated
- perf: the -O3 scan loss is code placement, not loop duplication; measured it
- perf: re-measure the five-language table; the Ada checks are not free
- perf: say what the clock actually covers, and name the machine
- readme: say up front that the index saves an agent's tokens, and that this is not an AI product
- readme: the skill's token saving is measured, so show the measurement
- readme: save, not file, since a file is also a thing you save
- readme: say plainly what the average costs you, and that nothing is guessed
- doc: SYNC.md command blocks lost their indentation and rendered as headings
- gitignore: asciinema casts are regenerated, not kept
- packaging: a page for distro maintainers and a reference PKGBUILD
- readme: the demo recording
- readme: lead with the demo recording
- demo: keys first, value last, the form --dump emits
- demo: tag the tunnels remote, and show one key gathering them
- demo: the real tunneling memo, anonymized, as the hook
- demo: use the is alias throughout, once it is defined
- demo: define the is alias up front, recall through it, ssh tunnel as the hook
- demo: git recall via the is alias, and no interactive step unless asked
- demo: a scripted terminal walkthrough for asciinema
- doc: check in ais --help as doc/command_line.txt, generated and held to the binary
- security policy: how to report, what is in scope, what is not
- style: say case, not arm
- style: document the checked-strcpy idiom, two more bounded heap sites, and the long dispatchers
- readme: say the opening in the words about.txt already uses
- readme: lowercase the name in prose, it is the command
- readme: lead with what it does and the CLI, move the model argument into Why
- fixing help
- AGENTS: add the rule that comments go stale like a README

## v0.3.17 (2026-08-03)

- tests: stop parsing an id out of --dump, which the format change removed
- format: --dump and --import speak the CLI's own grammar, KEY... -v VALUE
- man: sync the page with the code, and correct a claim that is now false
- docs: make the contract say what the code now enforces
- tests: pin the three invariants the planned redesigns would break silently
- ais_add: refuse a value another record already holds, in the pass it was making
- doc: --doc is not content-addressed, and the engine says it should be
- doc: FORMAT_V2 -- the dump/import grammar, and what hiding ids actually costs
- import: three paths that silently dropped records, and a posting that escaped idx/
- put: refuse a key beginning '-' instead of silently storing it untagged
- sync: --set's "this index syncs" warning was invisible to every LAN user

## v0.3.16 (2026-08-02)

- release: build number too (0.3.16+16)
- crypto: bound the KDF cost from an untrusted header, before it runs
- store: an over-long line must not be read as two, forging a record
- tests: assert /api/del actually deletes, and pin that it is idempotent
- tests: drive the Android app as the sync HOST, the half nothing tested
- tests: close the four gaps a coverage matrix found before the closed test

## v0.3.15 (2026-08-02)

- doc: how to drive the web and Flutter GUIs automatically, and what makes such a test worth having
- tests: drive a real sync on Android, since the desktop harness cannot build on this distro
- store: keep a torn write local, stage off and multi past the commit, cap documents as they stream, and make an attach note atomic
- AGENTS: document the two test layers added to close silent gaps
- app: only call an export a backup once it is actually saved somewhere
- app: host on the first free port instead of dead-ending when 8766 is taken
- android: sign with v3 so the upload key can be rotated later, and record why R8 stays off
- app: keep the screen awake while a QR is waiting to be scanned
- sync: say what a merge actually did, and stop --stats counting a multi-link record twice
- sync: join a peer by name as well as by address, and document the full wire vocabulary
- sync: say when a round leaves the devices different, keep keys on a legacy timestamp, and guard the FFI stack budget with a test
- sync: report a half-finished exchange as half done, not as a failure that copied nothing
- docs and packaging: stop promising Windows builds that are not published, list Android, and settle on AIS as the app name
- android: ship an adaptive icon so Android 8+ stops masking the legacy bitmap onto a shim
- tests: run the flutter sync UI test instead of letting it rot, open Sync with ctrl+shift+s, and gate the app in CI
- sync UI: name it backup, show when it last ran, keep a folder only if it works, and share one setting across app web and CLI
- app: recognise speech on-device only, and make the store icon, privacy policy and build docs match what ships
- store: mark the index v4 so an older ais refuses it instead of undoing edits it cannot see
- merge: keep the save path inside its stack budget, batch T| imports, and stop --del-under and M| leaving false clocks

## v0.3.14 (2026-07-31)


## v0.3.13 (2026-07-31)

- tests: give each script's second server a disjoint port; +1 collided with the next script
- flutter: one row per record in recall, so deleting cannot destroy links you never saw

## v0.3.12 (2026-07-31)

- backup: carry document bodies and keep a multi-link record whole across a restore

## v0.3.11 (2026-07-31)

- revert the tombstone salt: it broke delete propagation between devices
- ci: build the release with the version flags, not pubspec's placeholder
- doc: do not pin a versionCode figure that every commit moves
- doc: record the versionCode rule (git rev-list count), which is one-way
- flutter: re-run the version stamp when HEAD moves, not once per build dir
- flutter: stamp the engine version into libais.so, as the CLI build does
- deletes: keep a re-add, bound the tomb, salt the tombstone digest, and let a GUI compact

## v0.3.9 (2026-07-30)

- keys: add --untag (remove a tag, keep the records); --del-key -> --del-under, both previewing what they touch

## v0.3.8 (2026-07-30)

- engine: make a key attach durable in the store line; add --set to edit one value in place

## v0.3.7 (2026-07-17)

- doc: document K| detach line and --sync-folder; mark ktomb key-detach shipped
- flutter: fix FFI-seam use-after-free and isolate race, free-on-throw, and UI/UX defects
- engine: durable next_id recovery, atomic compact rollback, and audit hardening
- STYLE: point testing at make ut (whole suite), keep codeut as the engine-only subset

## v0.3.6 (2026-07-15)

- flutter: show result count only on Search view; clear paging cursor on empty query
- release: add iOS mic/speech usage strings; gitignore signing keys and err.log
- serve: refuse cross-origin browser calls to the API (CSRF: block index exfil/inject/delete)
- engine: stop compaction reusing a tombstoned id (silent loss); make key-detach survive an unaware peer (LWW)
- flutter: add Match any tag (OR) search toggle, parity with web
- web: keyset paging + infinite scroll for search, tags (parity with flutter)
- flutter: keyset paging + infinite scroll for search, tags, timeline
- engine: keyset paging for recall (ais_get_page) and tags (ais_tags_page)
- GUI: after save go to Recent with the new item on top (no search-box pollution); scanned ais:// deep link now pre-fills the Join dialog fields and dismisses any stale dialog

## v0.3.5 (2026-07-14)

- app version 0.3.5
- docs: usage example screenshot
- web: 'not on this device' badge for absent blobs / unreachable file refs (Flutter parity), in Search and Recent
- web: render encrypted rows in Recent with lock+Reveal (fillSecret), consistent with Search and Flutter
- web: reveal passphrase via inline field, not prompt() (disabled in installed PWA), matching Change Library
- defensive correctness + tests: store-line key/value sanitizing, dump-import round-trip, QR Reed-Solomon fix + v5 cap, robust HTTP header/body reads, PWA inline change-library, Add-save validation, qr-golden + widget tests

## v0.3.4 (2026-07-13)

- fix web CDP test: page opens on Recent now, so switch to Search view to set the absent-before-query baseline
- app version 0.3.4
- bump app version to 0.4.0
- ship-blocker fixes: flush pending delete before Change Library (no cross-library delete); switch view on save so the saved item is visible; block mutations during a LAN sync (cross-isolate store race)
- UI pass: fix sync-sheet overflow (scroll-controlled + scrollable), remove user-facing em-dashes, add versioning-cloud caveat (I5) to both GUIs, document mobile folder-picker limitation
- folder sync: drop the 20s background timer - purely user-driven (open, save, delete, explicit Sync now); web pushes after delete too
- engine: key-tombstone propagation (I1) - detach carries ts+hash, exports as K| lines, ais_merge_detach applies LWW, retained through compaction; tag removals now converge
- Flutter GUI: folder auto-sync - ais_embed_sync_folder FFI + Sync-folder setting (pick a Syncthing/cloud folder; runs on launch, after save, every 20s); FFI verify tool
- web GUI: folder auto-sync (/api/sync-folder + Sync-folder input in the sync sheet, remembered, runs on load and after save)
- engine: ais --sync-folder CLI (one pass per call) + document wall-clock LWW skew caveat (I2)
- engine: folder auto-sync core - framed bundle (B2), device-id clone heal via nonce+seq (B1), tombstone retention through compaction (B3), convergence tests
- web GUI parity E: deferred delete with Undo toast (replaces confirm dialog); data-id rows, flush-on-view-change
- web GUI parity D: file export/import (/api/export-bundle + /api/import-bundle with full Content-Length body read); File section in the sync sheet
- web GUI parity C: edit modal with in-place value edit + chip tag editor (drops the -key prompt); /api/keys + /api/setvalue routes; fix latent sheet overlay CSS
- web GUI parity B: dark mode via prefers-color-scheme (CSS vars + color-scheme for native controls)
- web GUI parity A: keys->tags wording, Timeline->Recent + open on content, underlined links, comma separators, empty-state Add CTA; fix latent timeline rowActions crash
- GUI polish: split multi-word tags into chips, cap list-row value at 3 lines, Enter submits Join and Library dialogs
- GUI sync: honest Hide (not fake Cancel) with a keeps-running note; feedback when a sync is already busy; no late failure snackbar after hiding
- GUI file transfer: native Save/Open dialogs via file_selector, default to Downloads; mobile export via share sheet
- GUI: accept comma as an optional key separator in Search and Add
- GUI a11y + naming: semantic underlined links, mic tooltip, readable-text contrast, 48dp targets; keys->tags wording, All->Recent
- fix GUI correctness cluster: undo race, engine swap, blob edit, detail delete, editKeys refresh, live search view
- sync: make file export/import plaintext (drop the seal); LAN sync stays sealed
- flutter: chip tag editor, file import/export, value edit, and UX standardization
- build: demote native win32 GUI to non-blocking; link ws2_32 in its check
- engine/ffi: visible-keys, sealed file bundle, in-place value edit, content find + tests
- ais skill keys-first store form; README notes the shipped skill
- roadmap
- README: See also agent-recipes
- minor
- README: silos pointer Q&A + passwords cross-platform framing

## v0.3.3 (2026-07-04)

- app: bump version to 0.3.3+3 for the Play release
- test: headless Flutter desktop sync UI harness (Xvfb/xdotool) + skill
- engine+app: single-source blob display (ais_doc_display + ais_embed_display FFI); web + Flutter share it
- app: show doc-blob content not its path; float bottom bar over keyboard
- docs: sync docs with code (roadmap, commands, Windows/Android, sync, store format)
- sync: fix Host-pane render -- bound _SyncWaitDialog width so the QR/card paints (QrImageView has no intrinsic size)

## v0.3.2 (2026-07-02)

- tests: recall cats doc-blob content, not the blobs/ path (match the fix)
- show doc-blob content not path (web+CLI); web save-modal + Windows GUI build/encrypt fixes; win32 CI gate
- readme: mark the Windows download temporarily unavailable (GUI rework); reversible one-liner
- release: stop publishing Windows (win32 GUI) zip + installer in releases
- cmake: CONFIGURE_DEPENDS on the engine glob so a new c/*.c links on incremental builds (no stale-cache undefined symbol)
- untrack generated .dart_tool/ (machine-specific abs paths broke apk builds on other machines)
- doc: native iOS client handoff spec (embed.h ABI, sync/pairing contract, xcframework build)
- sync: carry doc blobs over the wire + QR/deep-link scan-to-join (phone + web surface)
- sync: robustness -- fast distinct busy-port error (-3), LAN-IP prefers Wi-Fi, iOS local-network plist
- sync: one-tap unified Sync (Host/Join, bidirectional) -- app button + ais_embed_sync + 'ais --sync' CLI verb
- sync: symmetric bidirectional exchange (bidir flag; both converge in one round) + convergence test
- app: sync fixes -- INTERNET permission (release blocker) + barrier dialog on receive (data race)
- app: LAN sync Receive + Send (ais_embed_pull/serve, Flutter UI, forked-loopback tests)
- docs: on-demand LAN sync wording, README longevity hook, reusable sync blocks
- play store images
- cdp tests added
- docs: add PRIVACY.md (privacy policy for the Play Store listing)

## v0.3.1 (2026-07-01)

- flutter: bump app version to 0.3.1 for the release
- docs: add WHY-C.md (why C not Rust; safety via sanitizers + TDD)
- tests: rename targets to codeut/cliut/uiut + ut(all); drop check/test/suite/checkcli
- tests/README: document the browser UI layer (make uiut) and the make test alias
- perf: Rust merge PoC (no_std, zero-dep) + strip the Rust bench binary
- test: run the unit tests under ASan/UBSan (targets, pre-push hook, CI)
- gitignore: ignore Ada/GNAT build artifacts (*.ali, b~*)
- tests: browser UI render layer via headless Chrome (dump-dom), in the make suite GUI group
- tests: cover LWW branches, over-long guard, FFI encrypt/reveal, multi-value un-group, URL parsing; extract testable sync_parse_url
- perf: de-stale lang comparison, pin toolchain versions, fix SPARK table
- perf: SPARK proof of the merge (gnatprove: 0 unproved) + safety table
- perf: add Rust to the language benchmark + a safety-tier table
- perf: add Ada (GNAT) to the language benchmark + analysis
- safeguards for writing and perf tests
- perf: C vs Java vs Python benchmark (scan + intersection) + analysis
- doc: HOWTO_playstore appendix on automating uploads (fastlane / Play API)
- ci: build and attach the Play Store .aab in the android release job
- doc: HOWTO_playstore (closed test, 20 testers, build the .aab)
- iOS native support
- doc: plain text outlives the program that made it (the archive longevity point)
- doc: CLI screenshot shows two-key intersection (recall by AND)
- doc: add app and CLI screenshots to the README
- screenshots and package graph

## v0.3.0 (2026-06-30)

- sync: recreate socket per connect attempt (BSD/macOS cannot re-connect a failed fd)
- sync: portable accept timeout (poll), fixes macOS hang in sync_serve
- flutter: bump app version to 0.3.0 for the release
- about/README: document encrypted inline secrets + agent-safety positioning (not a password manager; secrets stay opaque to an agent, no vault to drain)
- doc: fix MERGE migration contradiction; trim man-page verbosity
- doc: core non-negotiables and sanctioned-heap whitelist (STYLE, AGENTS)
- gui: copy button on result rows and revealed secrets (web)
- util: dependency-free EXIF reader, directory tagger, and make check
- sync(security): token never on the wire -- challenge-response auth + domain-separated seal subkey (fixes the key-in-cleartext blocker); +export-side cap, SIGPIPE ignore, --token misuse guard, constant-time verify, seal version byte; docs corrected
- help: add --export / --export --serve / --import <url> --token to -h and --help
- doc/SYNC.md: document built-in one-shot LAN sync (ais --export --serve / --import <url> --token), alongside Syncthing
- sync(cli): wire ais --export --serve [PORT] / ais --import <url> --token T (sync_serve_lan/sync_pull_url, --token flag, pairing print + local-IP); man + spec updated
- SYNC_PROTOCOL.md: reconcile to as-built -- data-flow diagram, raw length-framed protocol (not HTTP), engine DONE; flag CLI + blob-transfer follow-ups
- sync(transport): sync_serve/sync_pull -- ephemeral single-client TCP transfer of the sealed merge stream (token auth, timeouts, length-framed); forked-loopback test
- ais --export: write the merge stream to stdout (CLI + man + cli test); 'ais -f A --export | ais -f B --import' merges A into B locally
- sync(transport): sync.c seal/unseal the merge stream over a wire (sync_export_sealed/import_sealed); authenticated, deletions propagate; test
- feed: feed_import_from(FILE*) so the sync client can merge a received stream (feed_import = stdin wrapper); cleaner round-trip test
- sync(transport): aisc_token -- high-entropy one-time pairing token (hex) + tests
- sync(transport): token-keyed AEAD seal/unseal (XChaCha20-Poly1305, blake2b(token) key, no Argon2) + tests
- MERGE.md: reconcile to as-built (identity=value, FNV-1a hash, no migration; mark implemented)
- sync(merge): merge-aware --import (A| add / D| delete, last-write-wins + resurrect via ais_put_at/ais_merge_del); tomb_lookup/tomb_remove; round-trip test
- docs: reduce redundancy, fix stale facts
- sync(merge): export-stream serializer (A|live records, D|tombstones) + tomb_each iterator + test
- sync(merge): tomb v2 'id|ts|hash' (compaction-proof content-addressed deletions); del/del-key stamp ts+hash via ais_record
- sync(merge): content_hash keys on VALUE (record identity is the value; put dedups by value, keys union)
- sync(merge): content-hash helper for cross-device record identity (FNV-1a) + test; refine tomb format to id|ts|hash
- gui: unify the primary label on 'Search' across web/flutter/win32; capture GUI vocabulary doc
- sync docs: ktomb deferred to follow-up — all design decisions now locked
- android: ais launcher icon; sync docs: dump stays readable + pickup-ready status/build-order
- win32: Recall/Timeline/Tags view tabs
- gui: per-row actions behind an overflow menu (web + flutter)
- sync: final CLI (--export / --import <url>, no --remote, merge-aware import) + tombstone-merge design note
- sync spec: correct merge model (no index --merge today) + decision B (full tombstone-union merge)
- docs: roadmap + spec for built-in E2E LAN sync (--offer/--pull)
- gui: bottom-nav Search/Timeline/Tags + date type-scale (web + flutter)
- docs: Syncthing sync HOWTO (doc/SYNC.md) + man SYNCING section

## v0.2.5 (2026-06-28)

- WHY.md: admit the usability cost (passphrase friction), worth it for the few secrets that matter
- WHY.md: cite Argon2 cost (64 MiB/3 passes), fix Kerckhoffs/implementation slide, scope 'at rest'
- drop locate-test debug print (macOS fix confirmed green)
- WHY.md: temper overclaims from critic pass (bound the agent claim, drop CNSA framing, honest differentiator)
- macOS: run locate test from a neutral cwd (avoid find_local home-boundary symlink edge)
- macOS: canonicalize the test home override so find_local stops at home
- WHY.md: agents run as you, so login-gated credential stores are exposed
- macOS: locate test matches the path tail (/tmp -> /private/tmp symlink)
- win32: link secret/b64/crypto (embed encrypt); macOS locate test realpath fix
- crypto/secret: Windows build (gate tty/mlock, rand_s) + macOS locate test fix
- flutter, uitests, apk
- ais_crypto: default Argon2 to 64 MiB (was 256) so vaults open on low-end phones
- SIGNING.md: add Android release-keystore + CI signing-secrets section
- release: build and attach the Android apk, with a per-ABI libais.so check
- android
- encryption, docs update, android support finished
- android support and docs
- crypto wired
- crypto implementation
- encryption module added
- Add --import-interactively: review records from another index, take picks (y/N)
- codesign declined
- MCP experiment for the paper
- cleaning

## v0.2.4 (2026-06-17)

- dialog for changing index dir
- doc
- doc
- doc
- standardizing UTC, docs
- Windows first-run note

## v0.2.3 (2026-06-17)

- date ranges, recovering the old behaviour OR and AND from the temporary introduced reversed

## v0.2.2 (2026-06-16)

- adding pager, cursor-like, removing TK gui version, flutter sync, delete made CRUD, dialog bug

## v0.2.1 (2026-06-16)

- adding pager, cursor-like, removing TK gui version, flutter sync, delete made CRUD, dialog bug

## v0.1.13 (2026-06-16)

- index localtion, removing env vars

## v0.1.12 (2026-06-16)

- removing find from dir
- checkbox inverted, as per feedback, key-value positions swapped

## v0.1.11 (2026-06-15)

- win32 GUI: enable visual styles via app manifest (native themed look)
- win32 GUI: Tab focus cycling + inline per-field labels
- win32 GUI: rename Recall->Get, add field labels for the two areas
- windows: fix recall (binary file mode) + readable DPI-aware GUI font

## v0.1.10 (2026-06-15)

- windows: portable zip as primary download; installer cleans Cygwin orphans
- windows: link ws2_32 in the native GUI (win.c references WSAStartup)
- windows: shim lstat -> stat for the native MinGW build
- windows: fix native (MinGW) cross-compile of post.c
- remove Cygwin entirely: no shipping path, no inert build bits
- windows: ship the native build in the installer (Cygwin-free)
- docs: macOS Gatekeeper/quarantine fix + notarization on roadmap
- PRIORITY: BDB backend considered pre-Oracle; its enclosure vindicates plain text
- docs: add ROADMAP (plans + non-goals), link from README
- docs: lead with 'extension of your associative memory'; frame as a working memex
- docs: state the core idea — files stay outside, immutable; index laid over them
- README: add Why (the variance idea) + Questions (FAQ)
- README: add a Download section pointing at /releases/latest

## v0.1.7 (2026-06-14)

- version: stamp the man page from the git tag too
- version: derive from the git tag (single source of truth)
- docs/PWA: fix sw.js icon ref; document the error-log convention
- doc: document the native Windows index location (%LOCALAPPDATA%\ais)
- native Windows: resolve the per-user index via SHGetFolderPath (no HOME/XDG)

## v0.1.6 (2026-06-14)

- win32 GUI: drop getenv -- resolve the log dir via OS APIs only
- win32 GUI: link shell32; resolve the log dir via SHGetFolderPath
- win32 GUI: error-only log file + crash handler

## v0.1.5 (2026-06-14)

- release: curate to one Windows asset (installer); plan native convergence
- release: clearer Windows asset names (installer vs portable)
- Native Win32 GUI: isolated no-browser desktop wrapper
- release: optional SignPath code-signing for the Windows installer
- WASM PWA milestone 1: engine -> WebAssembly build + CI
- WIP: native (Cygwin-free) Windows build via MinGW-w64
- Bundle the Cygwin LGPLv3 notice with the Windows artifacts

## v0.1.4 (2026-06-13)

- Icon, Windows installer, and Linux desktop integration

## v0.1.3 (2026-06-13)

- distributions as zip artifacts

## v0.1.2 (2026-06-13)

- GUI put: one paste -> one record; doc reorg; help + dist fixes

## v0.1.1 (2026-06-12)

- fixing crlf
- distribution for windows with cygwin1.dll

## v0.1.0 (2026-06-12)

- ci(release): drop Intel mac build (macos-13) — Apple-silicon only for now
- tests: normalize BSD wc -l leading pad in cli.sh (macOS portability)
- build: expose BSD/POSIX API on macOS via _DARWIN_C_SOURCE under -std=c99; drop per-symbol guard
- macos fix
- ignore
- wrong del
- cleaning
- claning screen in GUI after changing tab
- tag cloud and time range queries
- tcl bug
- gui
- docs
- move
- just docs
- fixing bug in android
- flutter / android support
- reorg
- arguments change
- should I write comments? yes
- cleaning python gui wrapper
- gui wrappers demos
- work
- optimization
- before change
- ver 1.1
- first basic version
- doc updated
- doc
- initial submit of shell scripts and first 2001-2005 implementation

## sourceforge-final-snapshot (2026-05-04)

- Import final SourceForge snapshot of an old project

## sourceforge-final-context (2026-05-04)

- re
- legacy code move
- legacy images

