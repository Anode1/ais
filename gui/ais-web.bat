@echo off
rem ais-web.bat -- double-click on Windows to open the AIS web GUI.
rem No Cygwin install needed to RUN; keep cygwin1.dll next to ais.exe.
rem Opens your browser, then runs the local server (close this window to stop).
start "" "http://127.0.0.1:8765/"
"%~dp0ais.exe" --serve
