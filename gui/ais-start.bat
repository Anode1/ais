@echo off
rem ais-start.bat -- Start-Menu launcher for the INSTALLED AIS (see installer/ais.iss).
rem Opens the web GUI in your browser, then runs the local server in this window
rem (close the window to stop it). Unlike ais-web.bat (the portable bundle's
rem launcher), it does NOT set AIS_INDEX, so it uses your default per-user index
rem -- your memory lives in your home folder, never inside the install dir.
start "" "http://127.0.0.1:8765/"
"%~dp0ais.exe" --serve
