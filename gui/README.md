# gui/ -- GUI wrappers (thin front ends over the `ais` CLI)

AIS is a command-line tool; the CLI is the portable, stable core. A GUI is a
thin front end that drives `ais` and renders its plain-text output -- it never
reimplements the engine, so it stays optional and replaceable.

## Two GUIs, both recall-first

Layout follows the v1 search look: a keys box on top, **Get** (or Enter) lists
the results one value per line with a `N results for Q - T ms` header; an
expandable **+ add** panel below holds put/doc (adding is the rarer action).

- **Desktop (Tk):** `wish gui/ais-put.tcl`  (or `./gui/ais-put.tcl`)
  Pure Tcl/Tk -- needs only `wish`. The maintained GUI. Values box (one per
  line) with **File…/Folder…** pickers, plus a Document box (`ais doc`).

- **Web:** `ais serve`  -- then open <http://127.0.0.1:8765/>
  Built INTO the binary (`c/serve.c`): no Python, no framework, no extra files.
  A tiny localhost HTTP loop serving an embedded page that calls the engine
  directly. The most portable surface (any browser); on macOS it needs only the
  built binary plus a browser. Binds 127.0.0.1 only (single user; do not expose).

## Why so thin

The CLI is the contract (`ais KEY...` -> `id|value` lines; `ais put -`,
`ais doc`, `ais dump`, ...). No GUI toolkit lasts forever, so the engine never
depends on one -- betting the app on a toolkit is the mistake; a thin, swappable
wrapper is the hedge. A GTK / Qt / Cocoa front end is equally possible: port the
~150 lines, keep the command line.

(Python wrappers -- a Tkinter twin and a stdlib web bridge -- were dropped once
`ais serve` gave a dependency-free web GUI, to avoid double maintenance.)
