# AIS — Roadmap

AIS is one small ANSI C engine (`c/`) with thin wrappers over a single FFI seam
(`embed.h`: `ais_embed_open` / `store` / `recall` / `timeline` / `tags` / …).
Almost everything below is a *wrapper* or a *packaging* task over that unchanged
engine — which is what keeps each piece tractable for one contributor at a time.
Help is welcome: open an issue to claim a piece.

## Shipped

- **Command line** (`ais`) — Linux, macOS, Windows.
- **Local web GUI** (`ais --serve`, 127.0.0.1 only) — the default GUI on every OS.
- **Native Windows app** (`win32/`, pure Win32 over the engine) — built; in testing.

## Planned

Roughly in priority order. Most are UI or platform glue; the engine changes only
for the two items flagged engine-level below (merging/sync and encrypted secrets).

### Mobile — Android and iPhone (PWA + Flutter) · next up

**This is the next focus**, to begin after the current round of tests, releases,
and publishing. The scaffolds already exist — a **PWA** (`app/`) and a **Flutter**
app (`app/flutter/`) — over the C engine through the FFI seam: `make lib` builds
the shared library and `embed.h` is the contract. Android first (simpler signing
and distribution), then iOS. The engine already compiles small and
dependency-free, so the work is UI + platform plumbing, not core changes.

### F-Droid (Android)

Publish the Android build on **F-Droid**, the free/open app store: a reproducible
build from source, no proprietary dependencies, plus the F-Droid metadata recipe.
Depends on the Android app above. Google Play is a separate, optional track.

### Seamless index merging and sync

An engine-level item (not just a wrapper). Today, combining two indexes
is manual: `ais --dump` from one piped into `ais --import` on another, or rsync
the plain files and rebuild. The goal is automatic, conflict-free merging — point
two indexes (or two copies of one) at each other and have them reconcile into the
union of records, de-duplicated, with the key index rebuilt. Because the store is
append-only plain text, merges compose cleanly (the same property that makes
rsync-style replication safe). This is the backbone of multi-device use: your
phone and laptop holding the same memory, with no central server.

**Transport — end-to-end encrypted, cloudless, in two layers:**

- **Today:** point Syncthing at the index folder, peer-to-peer, TLS + device keys,
  no cloud account. Documented in `doc/SYNC.md`.
- **Planned (built-in):** `ais --offer` / `ais --pull`, a one-shot LAN transfer with
  nothing to install. An ephemeral, single-client HTTP server whose body is sealed
  with XChaCha20-Poly1305 under a key derived from a one-time QR token (high entropy,
  so no Argon2, no PAKE, no TLS certs). The puller fetches the peer's
  store/tomb/blobs into a temp index, then runs the merge above. Same-LAN only by
  design; cross-network stays Syncthing's job. Spec: `doc/dev/SYNC_PROTOCOL.md`.

### Encrypted secrets — passwords under keys

The second engine-level item. Store secret values (passwords, tokens) encrypted
but retrievable by key like any other record: `ais` encrypts the value with a
user passphrase *before* it enters the store, so the on-disk file still holds
only opaque ciphertext text — the append-only, greppable, rsync-safe properties
are untouched (the field is simply unreadable without the key). Recall decrypts
on demand (an unlocked session, or a per-get passphrase prompt); secrets are a
distinct value class, never emitted in plaintext by `--dump`. Constraints, to
stay true to the project: a small, dependency-free, audited primitive (e.g. a
single-file ChaCha20-Poly1305 / AES-GCM — never a heavyweight crypto library or
hand-rolled cipher), authenticated encryption (tamper-evident), and an explicit
passphrase→key derivation (a KDF). This is the one place AIS would hold data it
cannot itself read — so the design bar (and review) is correspondingly higher.

### Speech support

Voice as a first-class input: **speak to file** (PUT) and **speak to recall**
(GET). On-device recognition where the platform provides it (iOS and Android
native speech APIs — not browser Safari, which is one reason iOS needs a native
shell). This is the seam toward the longer-horizon hands-free / wearable use.

### Native macOS app

A native macOS wrapper over the engine (as `win32/` is for Windows), so Mac users
get a real app, not only the web GUI via a launcher. A minimal AppKit/Swift shell
calling `embed.h`.

### Signing and notarization

So a *downloaded* build runs without security warnings. **macOS notarization**
(Apple Developer ID: `codesign` + `notarytool` + `staple` in CI) would remove the
"Apple could not verify 'ais' is free of malware" Gatekeeper block on downloaded
binaries, but it requires the paid Apple Developer Program ($99/year) and is not
planned. Meanwhile, clear the quarantine flag once with
`xattr -dr com.apple.quarantine .` (see the README), verify a download by its
SHA-256, or just build from source, which is never quarantined. **Windows code-signing** is already wired into the
release workflow (the SignPath OSS program), but is not active: SignPath's
Foundation program declined the project in June 2026 as too new — it gates on
community-adoption signals (stars, forks, third-party references) that a fresh
repo cannot yet show. Paid signing is not planned. The wiring stays in place;
reapply once the project has visible adoption. Until a build is signed, verify a
download by its SHA-256 or build from source (see the README).

## Not planned (non-goals)

- **A .NET / WinUI wrapper.** The native Win32 app (`win32/`) already covers
  Windows with no runtime dependency, and .NET's framework churn works against
  the "tiny, dependency-free, built to outlive its own tools" goal. Win32 is a
  decades-stable API; a self-contained .NET build drags a large runtime for no
  capability a user can feel.
- **A heavyweight backend** (SQLite, a database, a server daemon). Plain text is
  the durability and transparency guarantee, not a limitation — see the README
  "Questions."
- **A cloud account or sync service.** Sync is peer-to-peer over your own files
  (see *Seamless index merging and sync*); nothing phones home, by design.

## How to contribute

- **Keep the core pure.** ANSI C lives in `c/`; platform code and any
  C++/Swift/Dart stays isolated in its own wrapper directory (`win32/`, `app/`,
  a future `macos/`).
- **Build the engine as a library:** `make lib`.
- **The contract is `embed.h`** (and the CLI). Wrappers call it; they never reach
  into the on-disk store format.
- Open an issue describing the wrapper or platform you want to take.

See [`dev/DISTRIBUTION.md`](dev/DISTRIBUTION.md) for the packaging plan and
[`dev/LAYOUT.md`](dev/LAYOUT.md) for the on-disk format and module map.
