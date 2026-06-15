# Distribution -- one standard download per platform

**Principle.** Keep the engine flexible (one ANSI C core + thin wrappers), but
present **one obvious, traditional download per platform**. Capability lives in
the repo; the Releases page stays minimal so users never have to ask "which one?"

## Headline release assets (the curated set)

| Platform | The one download | GUI the user gets |
|----------|------------------|-------------------|
| Windows  | `ais-<tag>-windows-x86_64.zip` (primary; `…-installer.exe` optional) | a **native window** (`ais-gui.exe`); delete the folder to uninstall. CLI `ais.exe` beside it |
| macOS    | `ais-<tag>-macos-arm64.zip`              | **web** (`ais --serve`) via the `.command` launcher |
| Linux    | `ais-<tag>-linux-x86_64.zip`, `…-arm64.zip` | **web** (`ais --serve`) via the `.desktop` launcher |
| Phones   | the PWA (hosted, later)                  | web |

Each asset ships a `.sha256`. **Windows ships two:** the **portable zip**
(primary — unzip, double-click `ais-gui.exe`, delete the folder to uninstall; no
registry, the Java/xcopy model) and the **installer** (optional, for a Start-Menu
entry). **Not shipped:** source bundles or duplicate engines. The CLI is present
under every download.

Rule of thumb, matching how normal apps ship: **Windows gets a native Windows
app; the unix desktops get the universal web GUI; the CLI is under everything.**

## Plan A -- Windows: converge on the native build (DONE; one step left to verify)

Windows now ships the **native (MinGW) build**, no `cygwin1.dll`. Status:

1. Native build proven on CI (`native-windows.yml`). **Remaining:** confirm
   `ais-gui.exe` runs on real Windows (Marina) before relying on it.
2. **Done** -- `release.yml` cross-compiles native `ais.exe` (CLI + `--serve`)
   and `ais-gui.exe` (native GUI) with MinGW-w64 on Linux, then assembles the
   installer on `windows-latest`.
3. **Done** -- the installer bundles both; the Start-Menu shortcut launches
   `ais-gui.exe`; `ais.exe` is on PATH only (no console-flashing CLI shortcut).
   No `cygwin1.dll`, no `ais-start.bat`, no `ais.tcl`.
4. **Done** -- removed the Cygwin shipping path entirely: `THIRD-PARTY-NOTICES.txt`,
   `gui/ais-web.bat`, `gui/ais-start.bat`, and the `dist.sh` Cygwin branch. There
   are effectively no Cygwin users to serve (WSL and native MinGW cover that
   ground), so nothing Cygwin is shipped or built.

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
