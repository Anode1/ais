# gui/ -- the double-click launchers

Three one-line launchers that start `ais --serve` and let the browser be the GUI,
one per desktop:

    ais-web.bat        Windows
    ais-web.command    macOS
    ais-web.desktop    Linux

They ship inside the platform zips, so a user who never opens a terminal still
gets the web GUI. Which front ends exist and which one each platform gets is in
[`../doc/dev/DISTRIBUTION.md`](../doc/dev/DISTRIBUTION.md); what they must look
like is [`../doc/dev/GUI.md`](../doc/dev/GUI.md).

## Which index a launcher opens

A GUI is the same engine behind a window, so it resolves the index exactly like
the CLI, and reads no environment variable to do it:

    -f DIR  >  the nearest .ais/ at or above the working directory (git-style)
            >  the saved default in ~/.ais/config  >  ~/.ais

Each launcher `cd`s to **its own folder** first. So a `.ais/` sitting next to the
launcher is what the git-style walk finds, with no `-f` and no configuration:
copy an index in beside it and that is the one that opens. Otherwise it opens the
saved default, or `~/.ais`.

`ais --serve` typed by hand follows the same order, so it opens the saved default
unless you `cd` into a tree that has a `.ais/` or pass `-f`.

To change the default for good, use the **change** control next to the store path
in any of the front ends, or run `ais --switch -c NAME DIR` once.

## Why they are this thin

The CLI is the contract, and no GUI toolkit lasts forever, so the engine never
depends on one: a front end drives `ais` and renders its plain-text output.
Porting to GTK, Qt or Cocoa is a rewrite of the wrapper, not of the product. (Two
Python wrappers were dropped once `ais --serve` gave a dependency-free web GUI
rather than maintain both.)
