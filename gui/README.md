# gui/ -- the double-click launchers

Three launchers that start `ais --serve` and let the browser be the GUI, one per
desktop:

    win/ais-gui.exe    Windows   (built from win/ais-gui.c; the installer's shortcut target)
    ais-web.command    macOS
    ais-web.desktop    Linux

They ship inside the platform zips, so a user who never opens a terminal still
gets the web GUI. The Linux one runs `ais` from the PATH, so it works only once
`ais` is installed there. Which front ends exist and which one each platform
gets is in [`../doc/dev/DISTRIBUTION.md`](../doc/dev/DISTRIBUTION.md); what they must look
like is [`../doc/dev/GUI.md`](../doc/dev/GUI.md).

## Windows: ais-gui.exe beside ais.exe

The two sit in one folder, as `vim.exe`/`gvim.exe` and `git.exe`/`git-gui.exe`
do. `ais.exe` is the command line; double-clicked, it prints its help, says
where the window is, and waits for Enter instead of closing at once.
`ais-gui.exe` is the window: a program with no console that runs `ais.exe
--serve` hidden, opens the page in the default browser, and puts an icon in
the notification area (left-click: the page again; right-click: Open AIS,
Quit AIS). Quit ends the server, as closing the console does on macOS and
Linux. A second double-click, or the Start-menu entry while it runs, opens
the page and starts nothing. The installer ([`../installer/ais.iss`](../installer/ais.iss))
gives `ais-gui.exe` the Start-menu entry "AIS" and the Desktop icon, and puts
the folder on the PATH for `ais`; the portable zip holds the same two files
with no shortcuts. `make -C gui/win` cross-compiles it with MinGW-w64;
`tests/gui/windows-launcher.sh` drives it on a Windows runner.

## Which index a launcher opens

A GUI is the same engine behind a window, so it resolves the index exactly like
the CLI, and reads no environment variable to do it:

    -f DIR  >  the nearest .ais/ at or above the working directory (git-style)
            >  the current named index (or saved default) in ~/.ais/config  >  ~/.ais

The Windows and macOS launchers `cd` to **their own folder** first. So a `.ais/`
sitting next to one is what the git-style walk finds, with no `-f` and no
configuration: copy an index in beside it and that is the one that opens. Otherwise it opens the
saved default, or `~/.ais`. The Linux entry has no `Path=`, so it starts in
whatever directory the desktop picks, usually your home.

`ais --serve` typed by hand follows the same order, so it opens the saved default
unless you `cd` into a tree that has a `.ais/` or pass `-f`.

To change the default for good, use the **change** control next to the store path
in any of the front ends, or run `ais --switch -c NAME DIR` once.

## Why they are this thin

The CLI is the contract, and no GUI toolkit lasts forever, so the engine never
depends on one: the web GUI is inside the binary, and the Flutter app links the
engine through `c/embed.h`. The Windows launcher is Win32 alone, some 300 lines,
and never touches the index.
Porting to GTK, Qt or Cocoa is a rewrite of the wrapper, not of the product. (Two
Python wrappers were dropped once `ais --serve` gave a dependency-free web GUI
rather than maintain both.)
