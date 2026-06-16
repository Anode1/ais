# win32/ -- native Win32 GUI (isolated wrapper)

A no-browser, native Windows GUI for AIS: a real window, instant start, a tiny
self-contained `.exe` with **no .NET, no Cygwin, no bundled runtime** -- it links
only `user32`/`gdi32`/`comctl32`, present on every Windows (XP -> 11).

It is a **thin wrapper** over the engine's public FFI seam, exactly like
`ais --serve` and the Flutter app:

    win32/ais-gui.c  --(embed.h: ais_embed_open/store/recall/...; locate.h)-->  the C engine

The pure ANSI C core in `../c/` is never touched -- `windows.h` lives only here.
This directory is the one place any Windows-GUI code may go (and could host a C++
toolkit later, kept isolated the same way).

## Build (MinGW-w64, cross-compiled from Linux or native)

    make -C win32 CC=x86_64-w64-mingw32-gcc

Produces `win32/ais-gui.exe`. CI builds it in
`.github/workflows/native-windows.yml`. It compiles a curated subset of the
engine (core + `embed` + `locate` + the `win` shims) -- no `main`/`serve`/`feed`,
since the GUI needs no CLI, web server, or directory-walk.

## What it does

- A **keys** box + **Recall** (with an **OR** toggle) -> results list.
- Double-click an `http(s)` result to open it in the browser.
- A **value** + **keys** row + **Add** to store a new record.
- Opens the same default index the CLI uses (`ais_locate`): `-f`/nearest
  `.ais/`/the saved default in `~/.ais/config`/`~/.ais`.

It coexists with `ais --serve` (the cross-platform browser GUI); native users who
want a real window without a browser tab use this.
