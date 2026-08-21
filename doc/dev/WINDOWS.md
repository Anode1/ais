# Windows: what is built, and what is planned

**Nothing is published for Windows right now.** `release.yml` has no Windows
runner, so no CLI, no native GUI and no installer are attached to a release; a
Windows user builds from source, runs the Android app, or reaches a machine on
the LAN running `ais --serve`. This is the one place that status is stated;
[`DISTRIBUTION.md`](DISTRIBUTION.md) covers what every other platform gets.

The port is parked rather than abandoned, and it is parked on the desktop GUI
rework. Everything below is either gated in CI already or a settled plan waiting
for someone to build it.

## What is built today

`native-windows.yml` cross-compiles with MinGW-w64, no Cygwin and no
`cygwin1.dll`, in two jobs that gate differently:

- **win32-gui** builds the native window (`win32/`, a curated subset of the
  engine: the core plus `embed`, `locate` and the `win` shims, no
  `main`/`serve`/`feed`). It is the shipping Windows client, so it runs on every
  push and PR touching `c/` or `win32/` and must stay green. It broke silently
  once when LAN sync put SIGPIPE and sockets into `embed.c`; running it
  automatically is what stops that recurring. Its output is a CI artifact, never
  a release asset.
- **cli** builds the command line, manually (`workflow_dispatch`) and
  non-gating, because it pulls in `sync.c`, which is not ported to Winsock. That
  port is the out-of-scope item at the end of the next section.

## Sync: a file bundle instead of sockets

LAN sync (`sync.c`) is not portable to Windows: raw BSD sockets, `poll()`,
`signal(SIGPIPE)`, and no Winsock init. The Windows build is also GUI-only, so a
Windows user today has **no sync path at all**, neither LAN nor folder.
`embed.c`'s sync FFI is stubbed under `#ifdef _WIN32` purely so the build links.

Key insight: **sync = merge + transport, and only the transport is unportable.**
The merge (LWW / content-hash CRDT in `merge.c`) is fully portable and already in
the Windows build. A **file** is a valid transport: `export` a mergeable bundle,
move it by any means (USB, share, cloud), `import` it (merges, LWW) on the other
device, and that is the same convergence LAN sync gives, minus the socket.

### Decisions

- **Format: a single self-contained plaintext bundle** (chosen over encrypted).
  Reuses the exact bundle `sync` already builds: `<version byte>` then zero or
  more `B|<relpath>|<size>\n<raw bytes>` blob frames, then the
  `A|ts|keys|value` / `D|ts|hash` merge stream. Blobs are **included**, so one
  file carries documents too. Plaintext matches AIS's "plain text you own"
  philosophy and needs no passphrase; `aisc:` secrets stay encrypted inside it
  (their values are already encrypted at rest).
- **Not a folder copy.** Copying one `.ais/` over another overwrites, losing the
  target's records. Sync must go through export then import, which is the merge.
  A raw copy is only valid one-way, to move an index to a fresh PC.

### The wrinkle: MinGW has no `open_memstream`/`fmemopen`

`sync_export_sealed`/`sync_import_sealed` use both, so `sync.c` cannot compile
as-is for Windows. The **file** path avoids the memstream (it reads and writes a
real `FILE*`). The only remaining use is the record-merge step in import
(`fmemopen(rectext,...)` -> `feed_import_from`): replace it with a Windows-safe
path, either a `feed_import_str(ais*, const char*)` helper, or write `rectext` to
a temp file and `feed_import_from` that.

### Build plan

1. **`c/bundle.c` + `c/bundle.h`**: factor the portable bundle logic out of
   `sync.c`.
   - `int bundle_write(ais *a, FILE *out);` version byte + blob frames
     (`export_blobs`/`export_one_blob`) + `feed_export`.
   - `int bundle_read(ais *a, FILE *in);` parse blob frames (`import_one_blob`
     plus the `renmap` keep-both rename and `ren_rewrite`), then merge the record
     text (see the `fmemopen` note above).
   - Move `export_blobs`, `export_one_blob`, `import_one_blob`, `same_content`,
     `ren_add/ren_free/ren_rewrite` and `struct renmap` from `sync.c` into here.
   - `sync_export_sealed`/`sync_import_sealed` then become memstream +
     `bundle_write`/`bundle_read` + seal/unseal. The existing sync tests verify
     the refactor: same wire format, same round trip.
2. **FFI** (`embed.c`/`embed.h`): `int ais_embed_export_file(void *h, const char *path);`
   and `int ais_embed_import_file(void *h, const char *path);`, which `fopen` the
   path and call `bundle_write`/`bundle_read`. Portable: no sockets, no memstream.
3. **The native GUI** (`win32/`): **Export** and **Import** buttons. Export ->
   `GetSaveFileNameA`, import -> `GetOpenFileNameA`, both defaulting to Documents
   via `SHGetFolderPathA(CSIDL_PERSONAL)` with a default name like
   `ais-export.aisync`. **Never** default to `%LOCALAPPDATA%`: it is hidden.
   Documents is visible and writable, and the user picks the final spot in the
   dialog anyway. Comdlg32 is already available; add `-lcomdlg32` to
   `win32/Makefile` WINLIBS.
4. **CLI** (`main.c`): `ais --export FILE` / `ais --import FILE` through the same
   `bundle_write`/`bundle_read`, so every surface agrees. The existing
   stdin/stdout merge-stream behaviour stays; the FILE argument adds the
   blob-inclusive bundle.
5. **Makefiles**: `c/Makefile` globs `bundle.c` automatically; add it to the
   `win32/Makefile` ENGINE list.
6. **Test**: a round trip (export index A to a file, import into empty index B,
   assert B equals A including a blob-backed document), reusing the merge-test
   scaffolding in `tests.c`.

### Out of scope for that work

Porting `sync.c`'s sockets to Winsock for real LAN sync, and a full Windows CLI
build. `serve.c` is already Winsock-aware; `sync.c` is not.

## Signing: SignPath (planned, nothing runs today)

No `SIGNPATH_*` variable is set on the repository and there is no `sign-windows`
job. Adding the jobs below is part of the work, not a step already done.

Unsigned Windows downloads trigger SmartScreen's "Windows protected your PC /
Unknown publisher". The release workflow *would* sign the installer with
**SignPath.io**, which offers free code signing for OSS projects, a good fit for
AIS (GPL, on GitHub). Signing is meant to be **optional and additive**: a
`sign-windows` job would run only when SignPath is configured (the repository
variable `SIGNPATH_ORGANIZATION_ID` is set), so releases can ship unsigned until
then and nothing breaks.

### One-time setup

1. Apply for the **open-source plan** at https://signpath.io and create an
   **organization**.
2. Install the **SignPath GitHub app** and connect this repository, so SignPath
   can fetch the build artifact to sign.
3. In SignPath create a **project** (e.g. slug `ais`), an **artifact
   configuration** that signs the `*-windows-x86_64-installer.exe` inside the
   `ais-windows-x86_64` artifact (Authenticode), and a **signing policy** (e.g.
   slug `release-signing`).
4. Create a SignPath **API token** for a CI user.

### Repository configuration (Settings -> Secrets and variables -> Actions)

Secret:

- `SIGNPATH_API_TOKEN`

Variables:

- `SIGNPATH_ORGANIZATION_ID` (its presence is what enables the job)
- `SIGNPATH_PROJECT_SLUG` (e.g. `ais`)
- `SIGNPATH_POLICY_SLUG` (e.g. `release-signing`)
- `SIGNPATH_ARTIFACT_CONFIG_SLUG`

### How it would flow in release.yml

1. `build-windows` builds the installer and uploads it as `ais-windows-x86_64`,
   exposing the artifact id.
2. `sign-windows` submits that artifact to SignPath, downloads the **signed**
   installer, refreshes its `.sha256`, and uploads `ais-windows-signed`.
3. `publish` assembles the release, **overlaying the signed installer** over the
   unsigned one, then attaches everything.

Two notes. SmartScreen reputation still builds over time with a standard
(OV-style) certificate, which is what SignPath's OSS certificate is; an EV
certificate clears the warning immediately. And signing covers the **installer**
here: to sign the bundled executable as well, so running it directly never warns,
add it to the SignPath artifact configuration.

## Packaging: the installer and winget

`installer/winget/` holds the winget manifests. They are the template for a
future submission, not submittable as they stand: the only version directory is
twelve releases old and its `InstallerUrl` points at an artifact that no longer
exists. That directory's README has the regeneration steps.

## When a Windows build returns

Publish one native download again (a portable zip plus the optional installer,
the registry-free xcopy model, a Start-Menu shortcut, the CLI on PATH): add the
runner back to `release.yml`'s matrix, add the signing job above so SmartScreen
has something to trust, and regenerate the winget manifests against the new tag.
