# gui/ -- GUI wrappers (thin front ends over the `ais` CLI)

AIS is a command-line tool; the CLI is the portable, stable core. A GUI is a
thin front end that drives `ais` and renders its plain-text output -- it never
reimplements the engine, so it stays optional and replaceable.

One engine (CLI = contract) with thin front-ends over the embed FFI seam: web
(`ais --serve`), Flutter mobile, native Win32 (niche/legacy, see win32/README.md);
browser PWA/WASM is a planned future track.

## The web GUI, recall-first

Layout follows the v1 search look: a keys box on top, **Get** (or Enter) lists
the results one value per line with a `N results for Q - T ms` header; an
expandable **+ add** panel below holds put/doc (adding is the rarer action).

- **Web:** `ais --serve`  -- then open <http://127.0.0.1:8765/>
  Built INTO the binary (`c/serve.c`): no Python, no framework, no extra files.
  A tiny localhost HTTP loop serving an embedded page that calls the engine
  directly. The most portable surface (any browser); on macOS it needs only the
  built binary plus a browser. Binds 127.0.0.1 only (single user; do not expose).

## Which index a GUI uses

A GUI is the same `ais` engine behind a window, so it resolves the index exactly
like the CLI (no env vars): `-f DIR` > nearest `.ais/` at or above the working
directory (git-style) > the saved default in `~/.ais/config` > `~/.ais`.

The double-click launchers `cd` to their **own folder** before running
`ais --serve`, so a `.ais/` shipped **next to the launcher** is found by the
git-style walk, with no `-f`. To **seed** it, drop a `.ais/` beside the launcher
(for example, copy one from another project) and the GUI opens that one;
otherwise it opens your saved default, or `~/.ais`. To switch indexes for good,
use the GUI's "Store…" chooser (it persists via `ais --default`) or run
`ais --default PATH` once.

`ais --serve` run by hand follows the same resolution: your saved default
(or `~/.ais`), unless you cd into a `.ais/` tree or pass `-f`.

## Why so thin

The CLI is the contract (`ais KEY...` -> `id|value` lines; `ais -v -`,
`ais --doc`, `ais --dump`, ...). No GUI toolkit lasts forever, so the engine never
depends on one -- betting the app on a toolkit is the mistake; a thin, swappable
wrapper is the hedge. A GTK / Qt / Cocoa front end is equally possible: port the
~150 lines, keep the command line.

(Python wrappers -- a Tkinter twin and a stdlib web bridge -- were dropped once
`ais --serve` gave a dependency-free web GUI, to avoid double maintenance.)

## Planned: show and switch the store from the GUI

- **Show the current store** (the resolved index path) in each GUI's header, so
  you always know which index you are looking at.
- A **Store** button to switch the active index from inside the GUI (the web
  page, the native Windows app, and the Flutter app), so users never need `-f`
  or the command line to point at a different index.
