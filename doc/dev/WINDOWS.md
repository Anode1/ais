# Windows: what is built, and what is planned

**One zip per release, `ais-<tag>-windows-x86_64.zip`, cross-compiled from Linux
with MinGW-w64.** It holds `ais.exe`, the full CLI with the web GUI
(`ais --serve`, launched by `ais-web.bat`) and LAN sync over Winsock, so a
Windows user hosts and joins a QR sync with a phone exactly as on Linux; and
`ais-gui.exe`, the native window, search and add over the same index, no sync.
No Cygwin, no `cygwin1.dll`, no runtime. [`DISTRIBUTION.md`](DISTRIBUTION.md)
covers what every other platform gets.

## How it is built and tested

- `c/Makefile` recognises a `CC` naming mingw: gnu99 (so `<windows.h>` is not
  hidden by `__STRICT_ANSI__`), `-lws2_32 -lshell32`, and `.exe` names.
  `LDFLAGS=-static` folds in libgcc so the exe stands alone.
- `c/win.h` / `c/win.c` are the shims: Winsock init, `poll` as `WSAPoll`,
  `flock` as `LockFileEx`, `rename` as `MoveFileEx`, `fsync` as `_commit`,
  `realpath` as `_fullpath` plus `GetLongPathNameA` with forward slashes, a
  temp file in the user's temp dir (MSVCRT's
  `tmpfile()` opens one in the drive root, which a user cannot write), and the
  `SOCK_READ`/`SOCK_WRITE`/`SOCK_CLOSE` macros both platforms use, since a
  Winsock `SOCKET` is not a file descriptor. `sync.c` keeps its POSIX
  `open_memstream`/`fmemopen` and uses the temp file where MinGW has neither.
- `serve.c`'s Host forks a child on POSIX; on Windows it is a thread with a
  fresh handle to the same index, serialised by the store's per-handle file lock.
- `native-windows.yml` cross-compiles both exes on every push and PR touching
  `c/`, `win32/`, the two suites or itself, then runs `tests/cli.sh` and `tests/sync.sh` against
  `ais.exe` on a Windows runner. That job is the only place the Windows binary
  is ever executed: the developers' machines have no Windows and no wine. The
  engine's in-process tests (`c/tests.c`) fork and exec, so they are not built
  for Windows.
- `release.yml`'s `windows` job runs `scripts/dist.sh win`, which cross-compiles
  and packages the zip. The MCP bundle (`mcpb/`) still carries the Linux and
  macOS binaries only; `ais.exe` has `--mcp` but is not listed in its manifest.

## Known limits on Windows

- A key near the 255-byte limit can exceed `MAX_PATH` (260 characters for the
  whole path) once the index sits a few folders deep: the posting file for it
  cannot be created, and the record's store line is written without it.
  `tests/cli.sh` skips its 255-byte key test there. A long-path manifest would
  lift it only on systems whose policy allows long paths.
- No symbolic links, so the blob symlink refusal has nothing to refuse; the same
  test is skipped.
- A confirmation prompt (`--del` without `-y`, `-i`) asks at the console only
  when one of the standard streams is a console; a script with everything
  redirected gets the "no terminal to confirm on" exit instead of a hang. Under
  mintty (Git Bash) the standard streams are pipes, so the prompt refuses there
  too; cmd, PowerShell and Windows Terminal are consoles.
- A rename over a file another handle has open fails (POSIX replaces it): a
  put that lands while another process reads the same store is refused with
  an error, not corrupted. Run one writer at a time beside `--serve`.
- The store, `off` and `mts` are limited to 2 GiB: `long` is 32 bits there and
  every offset goes through it.

## Sync: a file bundle beside the sockets

The native window has no sync of its own. The LAN transport is now in its
engine (`embed.c`'s sync FFI is live on Windows), but the window shows no Host
or Join; the web GUI covers that. What the window would gain from the plan below
is a file bundle: export to a file, import from a file, the same merge.

Key insight: **sync = merge + transport, and the transport is the part a file
can replace.**
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

### MinGW has no `open_memstream`/`fmemopen`

Solved in `sync.c`: `bufstream_open`/`bufstream_close` and `strstream_open` are
the POSIX calls on POSIX and a temp file (`ais_tmpfile`) on Windows, so the
bundle code below needs no second path.

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

### Done already

The Winsock port of `sync.c` and the full Windows CLI build, which this plan
once listed as out of scope, shipped first; the bundle is what remains.

## Signing: SignPath (planned, nothing runs today)

No `SIGNPATH_*` variable is set on the repository and there is no `sign-windows`
job; the jobs below are still to be added.

Unsigned Windows downloads trigger SmartScreen's "Windows protected your PC /
Unknown publisher". The release workflow *would* sign the two exes in the zip with
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
   configuration** that signs `ais.exe` and the window's exe inside the
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

1. The `windows` job uploads the unzipped exes as `ais-windows-x86_64`,
   exposing the artifact id.
2. `sign-windows` submits that artifact to SignPath, downloads the **signed**
   exes, re-zips them and refreshes the `.sha256`, and uploads
   `ais-windows-signed`.
3. `publish` assembles the release, **overlaying the signed zip** over the
   unsigned one, then attaches everything.

SmartScreen reputation still builds over time with a standard (OV-style)
certificate, which is what SignPath's OSS certificate is; an EV certificate
clears the warning immediately.

## Packaging: the installer and winget

`installer/winget/` holds the winget manifests, the template for a future
submission. As they stand they cannot be submitted: the only version directory is
twelve releases old and its `InstallerUrl` points at an artifact that no longer
exists. That directory's README has the regeneration steps.

## When a Windows build returns

Publish one native download again (a portable zip plus the optional installer,
the registry-free xcopy model, a Start-Menu shortcut, the CLI on PATH): add the
runner back to `release.yml`'s matrix, add the signing job above so SmartScreen
has something to trust, and regenerate the winget manifests against the new tag.
