# Distribution: one standard download per platform

**Principle.** Keep the engine flexible (one ANSI C core + thin wrappers), but
present **one obvious, traditional download per platform**. Capability lives in
the repo; the Releases page stays minimal so users never have to ask "which one?"

## Headline release assets (the curated set)

| Platform | The one download | GUI the user gets |
|----------|------------------|-------------------|
| Windows  | _temporarily unavailable while the desktop GUI is reworked_ (use `ais --serve` or the mobile app meanwhile) | a **native window** (`ais-gui.exe`) when it returns |
| macOS    | `ais-<tag>-macos-arm64.zip`              | **web** (`ais --serve`) via the `.command` launcher |
| Linux    | `ais-<tag>-linux-x86_64.zip`, `…-arm64.zip` | **web** (`ais --serve`) via the `.desktop` launcher |
| Android  | `ais-<tag>-android.apk` (sideload) and `…-android.aab` (Play bundle) | the Flutter app |
| Phones (browser) | the PWA (hosted, later)          | web |

Each shipped asset (macOS, Linux, Android) is accompanied by a matching
`.sha256`. **Not shipped:** source bundles or duplicate engines. The CLI is
present under every desktop download.

Rule of thumb, matching how normal apps ship: the unix desktops get the universal
web GUI, Android gets the Flutter app, and the CLI is under every desktop
download.

## Windows

No Windows artifact is published at the moment, and the native build is
CI-validated rather than shipped. The whole picture, including the sync, signing
and packaging work that is planned rather than done, is in
[`WINDOWS.md`](WINDOWS.md).

## The GUI inventory

One engine (the CLI is the contract) with thin front-ends over the embed FFI
seam (`embed.c`), none needing a runtime:

- **web** (`ais --serve`, `c/serve.c`): the universal GUI, on every platform.
- **Flutter** (`app/flutter`): the mobile track.
- **native Win32** (`win32/ais-gui.c`): niche/legacy, the Windows native window.
- **browser PWA/WASM**: a planned future track (below).

What we keep maintaining: `c/` (the one engine), those front-ends, and one
installer + one archive per platform. Front-end label/layout conventions are in
`GUI.md`.

## Phones / PWA (planned)

A self-contained AIS that installs from a URL on iPhone, Android, and any desktop
browser: no app stores, no Apple Developer account, no GPL/App-Store conflict.
The index lives in the browser's own storage on the device (*your memory, yours
to keep, no server*).

Today's `app/` PWA is only a thin client to a local `ais --serve` (`/api/...`),
so it is a desktop convenience, not a standalone phone app. This track makes it
stand alone by compiling the engine to **WebAssembly** (emcc over the same
`embed.h` FFI seam the Flutter app uses) and keeping the store in browser
storage (IDBFS now, OPFS later). The WASM build curates engine + FFI only,
excluding the POSIX-bound `main.c`/`serve.c`/`feed.c`/`win.c`, so it dodges
sockets, nftw, realpath and `/dev/tty`; the one likely shim is `flock` -> no-op
(a browser origin is single-threaded).

Milestones, each verified on CI / a real phone: (1) `make wasm` emits
`app/engine/ais.{js,wasm}` exporting `ais_embed_*`; (2) mount IDBFS at the index
dir and `FS.syncfs` after each write; (3) in `app/`, call the WASM module when
present, else fall back to `fetch('/api/...')` (same page both ways, UI
unchanged); (4) GitHub Pages publishes `app/` and the Pages URL is the install
point ("Add to Home Screen"). The native Flutter apps remain a parallel option
(richer OS integration); the PWA is the broadest, lowest-friction reach.
