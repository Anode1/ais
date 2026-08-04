# Security Policy

## Reporting a vulnerability

Use GitHub's private vulnerability reporting on this repository
(**Security** tab -> **Report a vulnerability**). That keeps the report private
until there is a fix.

If that is unavailable to you, email **gavr144@gmail.com** with `ais security` in
the subject. Do not open a public issue for a vulnerability.

Please include what you need to make it reproducible: the version
(`ais --version`), the platform, the exact commands or input, and what you
expected instead. A failing test case or a store fragment that triggers it is
the most useful thing you can send.

Expect an acknowledgement within 7 days. I maintain this alone, so a fix may take
longer than that; I will tell you where it stands rather than go quiet. Please
give me 90 days before disclosing publicly, or less if the issue is already being
exploited. If you want credit in the release notes, say so and how you want to be
named.

## Supported versions

The latest release is the supported one. Fixes go into a new release rather than
being backported. Version scheme and release process are in
[`doc/dev/VERSIONING.md`](doc/dev/VERSIONING.md).

## What is in scope

- **The C engine** (`c/`): the store and index code paths, parsing of the store,
  posting lists, `--import` and `--dump`, and compaction. Memory-safety defects
  here are the highest priority. Anything reachable by feeding a crafted store,
  bundle, or import stream is in scope.
- **Encryption** (`c/crypto/`, `c/secret.c`): key derivation, the AEAD document
  format, `aisc:` values and blobs, and anything that would let a secret leak in
  cleartext where the design says it must not (piped, dumped, synced, or read
  programmatically).
- **Sync** (`c/sync.c`): the LAN protocol, its token handshake, session keys, and
  the merge path, including a peer that misbehaves or lies. The wire format is
  documented in [`doc/dev/SYNC_PROTOCOL.md`](doc/dev/SYNC_PROTOCOL.md).
- **The local web server** (`c/serve.c`): request parsing and the HTTP API. It
  binds 127.0.0.1 and is single-user by design; a way to reach it from off-host,
  or to escape the index directory through it, is a vulnerability.
- **Releases**: the GitHub Actions workflow that builds them, and the published
  checksums.

## What is out of scope

- **The plain-text store is readable by anything that can read the file.** That is
  the design, not a defect: the index is your data on your disk. Only values stored
  encrypted (`-e`, `aisc:`) are protected at rest. File permissions and disk
  encryption are the operating system's job.
- **An attacker who already has your passphrase, or code execution as your user.**
- **Unsigned desktop binaries** (macOS Gatekeeper warnings). Known and documented in
  the README; code signing is tracked in [`doc/ROADMAP.md`](doc/ROADMAP.md).
- **Denial of service by feeding a huge or corrupt store to your own index**, unless
  it also causes memory unsafety. Corruption is expected to stay local and
  recoverable; a crash on a malformed line is a bug, not a vulnerability.
- Vendored third-party code's own advisories, which belong upstream. The crypto
  primitives are Monocypher, vendored under `c/crypto/` (see
  [`c/crypto/README.md`](c/crypto/README.md)).

## What this project does to reduce the attack surface

- No network service runs unless you start one (`--serve`, `--sync`), and the web
  GUI binds loopback only.
- No dependencies to fetch and no runtime to install; a stock C99 toolchain builds it.
- The record path allocates nothing, which removes the leak, use-after-free and
  double-free class by construction. The memory discipline and its documented
  exceptions are in [`doc/dev/STYLE.md`](doc/dev/STYLE.md).
- The test suite runs under AddressSanitizer and UBSan on every push and in a
  pre-push hook.
- Releases are built by GitHub Actions in the open, and each artifact ships with a
  SHA-256 file you can verify (see the README).
