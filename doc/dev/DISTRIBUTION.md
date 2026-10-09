# Distribution: one standard download per platform

**Principle.** Keep the engine flexible (one ANSI C core + thin wrappers), but
present **one obvious, traditional download per platform**. Capability lives in
the repo; the Releases page stays minimal so users never have to ask "which one?"

## Headline release assets (the curated set)

| Platform | The one download | GUI the user gets |
|----------|------------------|-------------------|
| Windows  | `ais-<tag>-windows-x86_64.zip` (cross-compiled from Linux) | **web** (`ais --serve`) via the `.bat` launcher; a **native window** (`ais-gui.exe`) for search and add |
| macOS    | `ais-<tag>-macos-arm64.zip`              | **web** (`ais --serve`) via the `.command` launcher |
| Linux    | `ais-<tag>-linux-x86_64.zip`, `…-arm64.zip` | **web** (`ais --serve`) via the `.desktop` launcher |
| Android  | `ais-<tag>-android.apk` (sideload), `…-android-arm64-v8a.apk` (sideload, one ABI) and `…-android.aab` (Play bundle) | the Flutter app |
| Phones (browser) | the PWA (hosted, later)          | web |

Each shipped asset (Windows, macOS, Linux, Android) is accompanied by a matching
`.sha256`. **Not shipped:** source bundles or duplicate engines. The CLI is
present under every desktop download.

Rule of thumb, matching how normal apps ship: the desktops get the universal
web GUI, Android gets the Flutter app, and the CLI is under every desktop
download.

## Windows

The zip is cross-compiled from Linux with MinGW-w64 and exercised by the CLI
suites on a Windows runner in CI. How it is built, what the native window still
lacks, and the signing and installer work that is planned rather than done, are
in [`WINDOWS.md`](WINDOWS.md).

## The GUI inventory

One engine (the CLI is the contract) with thin front-ends over the embed FFI
seam (`embed.c`), none needing a runtime:

- **web** (`ais --serve`, `c/serve.c`): the universal GUI, on every platform.
- **Flutter** (`app/flutter`): the mobile track.
- **native Win32** (`win32/ais-gui.c`): niche/legacy, the Windows native window.
- **MCP** (`ais --mcp`, `c/mcp.c`): the agent front end, with no GUI at all:
  an agent calls recall/find/tags/timeline as tools ([`../MCP.md`](../MCP.md)).
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
storage (IDBFS now, OPFS later). The WASM build (`WASM_SRC` in `c/Makefile`)
compiles the engine, the FFI and what `embed.c` reaches (`sync.c`, `feed.c`,
`secret.c`, `win.c`, the crypto) and leaves out `main.c`, `serve.c`, `help.c`
and the tests: no CLI, no listening sockets, no tty. The rule has never been
run (`wasm-pwa.yml` is manual dispatch only), so whether those files compile
under emcc is unverified; the one likely shim is `flock` -> no-op (a browser
origin is single-threaded).

Milestones, each verified on CI / a real phone: (1) `make -C c wasm` emits
`app/engine/ais.{js,wasm}` exporting `ais_embed_*`; (2) mount IDBFS at the index
dir and `FS.syncfs` after each write; (3) in `app/`, call the WASM module when
present, else fall back to `fetch('/api/...')` (same page both ways, UI
unchanged); (4) GitHub Pages publishes `app/` and the Pages URL is the install
point ("Add to Home Screen"). The native Flutter apps remain a parallel option
(richer OS integration); the PWA is the broadest, lowest-friction reach.
