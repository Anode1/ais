# Distribution -- one standard download per platform

**Principle.** Keep the engine flexible (one ANSI C core + thin wrappers), but
present **one obvious, traditional download per platform**. Capability lives in
the repo; the Releases page stays minimal so users never have to ask "which one?"

## Headline release assets (the curated set)

| Platform | The one download | GUI the user gets |
|----------|------------------|-------------------|
| Windows  | `ais-<tag>-windows-x86_64-installer.exe` | a **native window** (`ais-gui.exe`) after the convergence below; today, the web GUI via the Start-Menu shortcut |
| macOS    | `ais-<tag>-macos-arm64.zip`              | **web** (`ais --serve`) via the `.command` launcher |
| Linux    | `ais-<tag>-linux-x86_64.zip`, `…-arm64.zip` | **web** (`ais --serve`) via the `.desktop` launcher |
| Phones   | the PWA (hosted, later)                  | web |

Each asset ships a `.sha256`. **Not shipped:** the Windows portable zip (it's
only the installer's build input), source bundles, or duplicate engines. The CLI
is present under every download.

Rule of thumb, matching how normal apps ship: **Windows gets a native Windows
app; the unix desktops get the universal web GUI; the CLI is under everything.**

## Plan A -- Windows: converge on the native build (retire Cygwin from shipping)

Today Windows ships a **Cygwin** `ais.exe` (needs `cygwin1.dll` + the LGPL
notice). Target: a **single native (MinGW) Windows build**, no DLL.

1. Prove the native build on CI (`native-windows.yml`) and test `ais.exe` +
   `ais-gui.exe` on real Windows (Marina).
2. Switch the `build-windows` job in `release.yml` from the Cygwin toolchain to
   the **MinGW native** build, producing native `ais.exe` (CLI + `--serve`) and
   `ais-gui.exe` (native GUI).
3. The **installer** bundles both; the Start-Menu shortcut points at
   `ais-gui.exe` (the native window). Drop `ais-web.bat` + `cygwin1.dll`.
4. Remove the Cygwin shipping path, `THIRD-PARTY-NOTICES.txt`, and the LGPL
   notice wiring (no `cygwin1.dll` to attribute). Keep the Cygwin Makefile bits
   only as an internal fallback, **unshipped**.

Result: one Windows download, a native app, no runtime, no third-party DLL.

## Plan B -- retire the Tk GUI

`gui/ais.tcl` overlaps both the native Win32 window (Windows) and the web GUI
(everywhere), and it is the only front-end needing a runtime (`wish`/Tcl-Tk).
Remove it to cut a confusing third option and a maintenance surface:

- delete `gui/ais.tcl`, its copy in `scripts/dist.sh`, and references in
  `gui/README.md` and `doc/OVERVIEW.md`;
- it stays in git history if ever wanted.

GUIs then reduce to: **native Win32 (Windows) + web `--serve` (all) + PWA
(phones)** -- each with a clear, single purpose.

## What we keep maintaining

- `c/` -- the one C engine.
- Wrappers: `serve.c` (web), `win32/ais-gui.c` (native Windows), the WASM PWA,
  and the Flutter app (mobile track).
- One installer + one archive per platform.

## Sequencing

Do nothing that breaks current (Cygwin) releases until the native build is
proven on real Windows. Order: prove native -> switch Windows to native +
installer-bundles-GUI -> drop Cygwin/notice -> retire Tk. Each step is a small,
separate change; releases stay green throughout.
