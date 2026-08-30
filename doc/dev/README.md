# doc/dev: for developers and contributors

Implementation notes. The public docs are one level up in `doc/`, indexed by the
README's "Learn more" table. Start with `../../AGENTS.md`, then `LAYOUT.md` and
`STYLE.md`.

## The engine

| File | What |
| --- | --- |
| [LAYOUT.md](LAYOUT.md) | the on-disk format and the module map: where every file and every concept lives |
| [BNF.txt](BNF.txt) | the storage grammar a writer must produce and a verifier checks |
| [STYLE.md](STYLE.md) | coding ideology: C99, stack and streaming, the sanctioned heap, error idioms |
| [LOCKING.md](LOCKING.md) | reads take no lock, writers take one per op, and why `next_id` is disk-authoritative |
| [FORMAT_V2.md](FORMAT_V2.md) | the `--dump`/`--import` grammar, and why ids leave that surface and stay everywhere else |
| [WHY-PLAIN-TEXT.md](WHY-PLAIN-TEXT.md) | the answer to "use a binary DB", with the 1M-record measurements |
| [WHY-C.md](WHY-C.md) | the answer to "rewrite it in a memory-safe language", and how that bug class is caught instead |

## Sync

| File | What |
| --- | --- |
| [SYNC_DESIGN.md](SYNC_DESIGN.md) | the settled decisions: identity, keys as a patch, tombstones, sharing |
| [SYNC_PROTOCOL.md](SYNC_PROTOCOL.md) | the wire: handshake, sealed payload, blob frames, security model |
| [MERGE.md](MERGE.md) | how two indexes reconcile, verb by verb |

## Front ends

| File | What |
| --- | --- |
| [GUI.md](GUI.md) | what every surface must look like: vocabulary, layout, the two web pages |
| [GUI_TESTING.md](GUI_TESTING.md) | how to drive one without a human clicking, and without a window on a real display |
| [HTTP_API.md](HTTP_API.md) | the `--serve` endpoints both web front ends call |

## Shipping

| File | What |
| --- | --- |
| [VERSIONING.md](VERSIONING.md) | the four things that version independently, and the procedure for cutting a release |
| [PACKAGING.md](PACKAGING.md) | for distro maintainers: build, test, staged install, licences, the `AIS_VERSION` override |
| [DISTRIBUTION.md](DISTRIBUTION.md) | one download per platform, what each gets, and the PWA/WASM track |
| [ANDROID_RELEASE.md](ANDROID_RELEASE.md) | getting a build onto a phone, into Play, and onto F-Droid |
| [IOS_RELEASE.md](IOS_RELEASE.md) | the Apple side step by step: enrolment, signing without a Mac, TestFlight, the App Store |
| [IOS_TODO.md](IOS_TODO.md) | the release checklist, one box per step of IOS_RELEASE.md |
| [SPEECH.md](SPEECH.md) | voice input: the one in-app interaction, the OS entry points, and the build order |
| [WINDOWS.md](WINDOWS.md) | the whole Windows situation: what CI builds, and the sync, signing and packaging plans |

iOS is split in two so neither half goes stale: `IOS_RELEASE.md` owns the Apple
account and the path to the store, and
[issue #1](https://github.com/Anode1/ais/issues/1) owns the developer brief, the
engine wiring and the acceptance list to run on a real phone.

## Writing

[PROSE.md](PROSE.md) binds these documents the way `STYLE.md` binds the C: what
bold means, what a title may claim, and one home per fact.
