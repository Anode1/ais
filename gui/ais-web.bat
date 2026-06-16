@echo off
rem ais-web.bat -- double-click on Windows to open the AIS web GUI.
rem
rem Runs the ais.exe next to this file as a local server; ais --serve opens your
rem browser at http://127.0.0.1:8765/. This window stays open while the server
rem runs -- close it to stop. (For the native desktop app instead, run ais-gui.exe.)
cd /d "%~dp0"
rem cd here so a .ais next to this launcher is found by the git-style walk
"%~dp0ais.exe" --serve
