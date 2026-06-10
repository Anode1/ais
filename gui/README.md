# gui/ -- reference GUI wrappers

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

## Demo wrappers (committed)

Two equivalent ~50-line demos, same layout and behavior -- a keys entry, a
**values box (one value per line)**, a Put button, a status label:

    wish gui/ais-put.tcl        # pure Tcl/Tk; most self-contained
    python3 gui/ais-put.py      # Python/Tkinter twin, stdlib only

Both locate the binary the same way (`ais` on PATH, else
`/home/vas/ais/c/ais`) and feed the box to `ais put - KEY...` via their
language's argument-list exec (Tcl `exec`, Python `subprocess.run`), never a
shell string. The store is line-oriented, so **each non-blank line is a
separate value**, all filed under the keys -- e.g. two `curl` lines under
`kul dev version` recall together. A second **Document box** saves a multi-line
block as a file via `ais doc KEY...` (the index stores its relative path). (For
varied keys per line, the CLI `ais import` reads `keys|value` files.)

Recommendation: **Tcl/Tk or Python/Tkinter** for portability -- both run on
Linux, Windows, and macOS with no extra dependencies. Because the wrapper only
calls `ais put`, a GTK, Qt, Cocoa, or web front end is equally possible; port
the ~50 lines, keep the command line.

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

(The two demos above are committed; richer/per-toolkit wrappers go in subdirs.)
