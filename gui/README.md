# gui/ -- reference GUI wrappers (future)

AIS is a command-line tool; the CLI is the portable, stable core. GUIs for
non-CLI users live here as **thin wrappers that shell out to the `ais` binary and
parse its plain-text output** -- they never reimplement the engine. This keeps the
core portable, keeps the GUI optional and replaceable, and lets anyone add a
wrapper for their environment (GNOME, KDE, web, ...) without touching the engine.

## Why thin wrappers over the CLI

- The CLI is the contract: `ais get k1 k2` -> `id|value` lines; plus `ais dump`,
  `ais keys`, `ais put ...`. A wrapper runs these and renders the result.
- No GUI toolkit lasts forever (Tcl/Tk wrappers fine in 2005 were fragile by the
  2010s). The CLI is the invariant; the GUI is the variance. Betting the
  application on one toolkit is the mistake; thin, swappable wrappers are the
  hedge.

## Layout convention

One subdirectory per toolkit, each a self-contained wrapper:

    gui/<toolkit>/        e.g. gui/tkinter/, gui/web/, gui/gtk/, gui/qt/

## Suggested reference wrapper

For a minimal, portable-enough REFERENCE (not a production GUI):

- **Python + Tkinter** -- ships with Python, cross-platform, still maintained;
  the successor to the Tcl/Tk approach with far less setup. A few hundred lines:
  an entry for keys, a list for results, buttons that run `ais` and show output.
- **A local web page** -- the most universal surface (any browser, any OS), if a
  tiny local bridge to the CLI is acceptable.

Production GUIs (GTK for GNOME, Qt for KDE, native Windows/macOS) are expected to
be written per-environment by whoever needs them -- again, as thin callers of
`ais`, not as forks of the engine.

(No wrapper is committed yet; this directory marks where they go.)
