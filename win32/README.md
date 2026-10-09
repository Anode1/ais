# win32/ -- the native Windows window

> Niche: most users are better served by `ais --serve`, the same browser GUI
> every platform gets. It ships as `ais-gui.exe` in the Windows zip, and CI
> keeps it compiling on every change to `c/` or `win32/`. What is planned for
> Windows, and why the CLI is not built here: [`../doc/dev/WINDOWS.md`](../doc/dev/WINDOWS.md).

See `../gui/README.md` for why front ends stay thin.

## Build (MinGW-w64, cross-compiled from Linux or native)

    make -C win32 CC=x86_64-w64-mingw32-gcc

Produces `win32/ais-gui.exe` (CI: `.github/workflows/native-windows.yml`). It
compiles a curated subset of the engine (core + `embed` + `locate` + `sync` +
`feed` + `secret` + `b64` + the crypto + the `win` shims), no
`main`/`serve`/`help`/`tests`.

## What it does

A no-browser native Windows window over the engine's FFI seam
(`win32/ais-gui.c` -> `embed.h`/`locate.h`): a keys box + **Search** (with a
**Match any key** toggle) -> results list (double-click an `http(s)` result to
open it), and a **value** + **keys** row + **Add** to store. It opens the same default index
the CLI uses (`ais_locate`): `-f`/nearest `.ais/`/`~/.ais/config`/`~/.ais`.
`windows.h` lives only here; the pure ANSI C core in `../c/` is never touched.
