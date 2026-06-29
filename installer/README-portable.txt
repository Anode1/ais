AIS -- your memory, yours to keep.   (Windows, portable)

Three ways to use it, all from this folder:
  Desktop app:  double-click  ais-gui.exe
  Web GUI:      double-click  ais-web.bat   (opens in your browser)
  Command line: open a terminal here and run  ais.exe --help
                (or add this folder to your PATH to use 'ais' anywhere)

Portable -- nothing is installed: to remove AIS, just delete this folder (no
registry, no Program Files). Your memory (the index) lives in %USERPROFILE%\.ais,
NOT in this folder, so deleting it never touches your data; run ais.exe --where
for the exact path.

First run: this build is not yet code-signed, so Windows may show a blue
"Windows protected your PC" screen. That is SmartScreen flagging a new file,
not a virus warning -- click "More info", then "Run anyway" (once per file).

No Cygwin, no .NET, no runtime: the .exe files are self-contained.
New here? Open USING.txt for a one-minute guide, about.txt for what AIS is.
