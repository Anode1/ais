# Windows file-based sync (planned)

Status: **planned, deferred to a post-release commit.** Decisions below are settled;
this is the build spec for whoever implements it.

## Why

LAN sync (`sync.c`) is not portable to Windows — it uses raw BSD sockets,
`poll()`, `signal(SIGPIPE)`, and no Winsock init. The Windows build is also
GUI-only (`ais-gui.exe`); there is no Windows CLI. So today a Windows user has
**no sync path at all** (no LAN, no file). `embed.c`'s sync FFI is stubbed on
Windows (see `embed_pull`/`embed_serve`, `#ifdef _WIN32`) purely so the win32
build links.

Key insight: **sync = merge + transport, and only the transport is unportable.**
The merge (LWW / content-hash CRDT in `merge.c`) is fully portable and already in
the Windows build. A **file** is a valid transport: `export` a mergeable bundle,
move it by any means (USB, share, cloud), `import` it (merges, LWW) on the other
device — the same convergence LAN sync gives, minus the socket.

## Decisions

- **Format: a single self-contained plaintext bundle** (chosen over encrypted).
  Reuses the exact bundle `sync` already builds:
  `<version byte>` then zero or more `B|<relpath>|<size>\n<raw bytes>` blob
  frames, then the `A|ts|keys|value` / `D|ts|hash` merge stream. Blobs are
  **included**, so one file carries documents too. Plaintext matches AIS's
  "plain text you own" philosophy and needs no passphrase; `aisc:` secrets stay
  encrypted inside it (their values are already encrypted at rest).
- **Not a folder copy.** Copying one `.ais/` over another overwrites (loses the
  target's records). Sync must go through export→import (the merge). A raw copy
  is only valid one-way ("move my index to a fresh PC").

## The wrinkle: MinGW has no `open_memstream`/`fmemopen`

`sync_export_sealed`/`sync_import_sealed` use `open_memstream`/`fmemopen`, so
`sync.c` cannot compile as-is for Windows. The **file** path avoids the memstream
(it reads/writes a real `FILE*`). The only remaining use is the record-merge step
in import (`fmemopen(rectext,...)` → `feed_import_from`): replace it with a
Windows-safe path — either a `feed_import_str(ais*, const char*)` helper, or write
`rectext` to a temp file and `feed_import_from` that.

## Build plan

1. **`c/bundle.c` + `c/bundle.h`** — factor the portable bundle logic out of
   `sync.c`:
   - `int bundle_write(ais *a, FILE *out);`  — version byte + blob frames
     (`export_blobs`/`export_one_blob`) + `feed_export`.
   - `int bundle_read(ais *a, FILE *in);`    — parse blob frames
     (`import_one_blob` + the `renmap` keep-both rename + `ren_rewrite`), then
     merge the record text (see the `fmemopen` note above).
   - Move `export_blobs`, `export_one_blob`, `import_one_blob`, `same_content`,
     `ren_add/ren_free/ren_rewrite`, `struct renmap` from `sync.c` into here.
   - `sync_export_sealed`/`sync_import_sealed` then become: memstream +
     `bundle_write`/`bundle_read` + seal/unseal. Existing sync tests verify the
     refactor (same wire format, same round-trip).
2. **FFI** (`embed.c`/`embed.h`): `int ais_embed_export_file(void *h, const char *path);`
   and `int ais_embed_import_file(void *h, const char *path);` — `fopen` the path,
   call `bundle_write`/`bundle_read`. Portable (no sockets, no memstream).
3. **win32 GUI** (`win32/ais-gui.c`): **Export** and **Import** buttons.
   - Export → `GetSaveFileNameA` (default dir = Documents via
     `SHGetFolderPathA(CSIDL_PERSONAL)`, default name e.g. `ais-export.aisync`).
   - Import → `GetOpenFileNameA`, same default dir.
   - **Never** default to `%LOCALAPPDATA%` — it is hidden. Documents is visible
     and writable; the user picks the final spot in the dialog.
   - Comdlg32 is already available; add `-lcomdlg32` to `win32/Makefile` WINLIBS.
4. **CLI** (`main.c`): `ais --export FILE` / `ais --import FILE` through the same
   `bundle_write`/`bundle_read`, so every surface agrees. (Keep the existing
   stdin/stdout `--export`/`--import` merge-stream behavior; the FILE arg adds the
   blob-inclusive bundle.)
5. **Makefiles**: `c/Makefile` globs `bundle.c` automatically; add `bundle.c` to
   the `win32/Makefile` ENGINE list.
6. **Test**: a round-trip test (export index A to a file, import into empty index
   B, assert B == A including a blob-backed document). Reuses the merge-test
   scaffolding in `tests.c`.

## Out of scope (later)

- Porting `sync.c`'s sockets to Winsock for real LAN sync on Windows.
- A Windows CLI build (`ais.exe`); `serve.c` is already Winsock-aware, `sync.c`
  is not.
