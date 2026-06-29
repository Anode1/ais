AIS -- your memory, yours to keep.

First run: this build is not yet code-signed, so Windows may show a blue
"Windows protected your PC" screen. That is SmartScreen flagging a new file,
not a virus warning -- click "More info", then "Run anyway" (once per file).

Command line: open a NEW terminal and run
    ais --help            e.g.  ais venice italy
    ais --serve           open the web GUI in your browser instead
    ais --where           print where your memory is stored

Your memory (the index) lives in your user profile, NOT in the program folder:
    %USERPROFILE%\.ais
so removing AIS never touches your data. It is plain text you can read, back
up, or copy elsewhere.

No Cygwin, no .NET, nothing else to install: ais.exe and ais-gui.exe are
self-contained native programs.

Installed vs Portable:
  Installed (this build): Start Menu shortcut "AIS" launches the native window;
    remove via the uninstaller (Add/Remove Programs).
  Portable build: double-click ais-gui.exe in its folder; remove by deleting
    the folder (no registry, no Program Files).

New here? Open USING.txt for a one-minute guide, or about.txt for what AIS is.
