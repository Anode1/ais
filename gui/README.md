# gui/ -- GUI wrappers (thin front ends over the `ais` CLI)

AIS is a command-line tool; the CLI is the portable, stable core. A GUI is a
thin front end that drives `ais` and renders its plain-text output -- it never
reimplements the engine, so it stays optional and replaceable.

## Two GUIs, both recall-first

Layout follows the v1 search look: a keys box on top, **Get** (or Enter) lists
the results one value per line with a `N results for Q - T ms` header; an
expandable **+ add** panel below holds put/doc (adding is the rarer action).

- **Desktop (Tk):** `wish gui/ais.tcl`  (or `./gui/ais.tcl`)
  Pure Tcl/Tk -- needs only `wish`. The maintained GUI. Values box (one per
  line) with **File…/Folder…** pickers, plus a Document box (`ais --doc`).

- **Web:** `ais --serve`  -- then open <http://127.0.0.1:8765/>
  Built INTO the binary (`c/serve.c`): no Python, no framework, no extra files.
  A tiny localhost HTTP loop serving an embedded page that calls the engine
  directly. The most portable surface (any browser); on macOS it needs only the
  built binary plus a browser. Binds 127.0.0.1 only (single user; do not expose).

## Which index a GUI uses

A GUI is the same `ais` engine behind a window, so it resolves the index exactly
like the CLI: `-f DIR` > `$AIS_INDEX` > nearest `.ais/` above the working dir >
the per-user default (`~/.local/share/ais`).

The double-click launchers set `AIS_INDEX` to a `.ais/` **next to themselves**
(`ais-web.bat` uses `%~dp0.ais`; `ais-web.command` uses `$dir/.ais`), so the GUI
uses the index that ships in the bundle, with no `-f`. To **seed** it, drop a
`.ais/` beside the launcher (for example, copy one from another project), and the
GUI opens that one. To point elsewhere, set `AIS_INDEX` or pass `-f DIR`. (The
Linux `.desktop` launcher can't locate itself, so there `ais --serve` falls back
to the per-user default unless `AIS_INDEX` is set.)

The Tk GUI, or `ais --serve` run by hand, follows the plain resolution (no
`AIS_INDEX` preset): the per-user index, unless you cd into a `.ais/` tree or
pass `-f`.

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
- A **Store** button to switch the active index from inside the GUI (Tk, the web
  page, and the Flutter app), so users never need `-f` or the command line to
  point at a different index.
