@echo off
rem ais-web.bat -- double-click on Windows to open the AIS web GUI.
rem No Cygwin install needed to RUN; keep cygwin1.dll next to ais.exe.
rem Opens your browser, then runs the local server (close this window to stop).
rem Use the .ais index next to this file, so no -f is needed. Replace that .ais
rem folder to seed a starter index, or set AIS_INDEX / pass -f to point elsewhere.
set "AIS_INDEX=%~dp0.ais"
start "" "http://127.0.0.1:8765/"
"%~dp0ais.exe" --serve
