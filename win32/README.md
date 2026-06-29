# win32/ -- native Win32 GUI (legacy/niche, unmaintained)

> Legacy, niche, unmaintained wrapper. Most users should prefer `ais --serve`
> (the cross-platform browser GUI). This branch is parked.

See gui/README.md for why front-ends stay thin.

## Build (MinGW-w64, cross-compiled from Linux or native)

    make -C win32 CC=x86_64-w64-mingw32-gcc

Produces `win32/ais-gui.exe` (CI: `.github/workflows/native-windows.yml`). It
compiles a curated subset of the engine (core + `embed` + `locate` + the `win`
shims), no `main`/`serve`/`feed`.

## What it does

A no-browser native Windows window over the engine's FFI seam
(`win32/ais-gui.c` -> `embed.h`/`locate.h`): a keys box + **Recall** (with an
**OR** toggle) -> results list (double-click an `http(s)` result to open it), and
a **value** + **keys** row + **Add** to store. It opens the same default index
the CLI uses (`ais_locate`): `-f`/nearest `.ais/`/`~/.ais/config`/`~/.ais`.
`windows.h` lives only here; the pure ANSI C core in `../c/` is never touched.
