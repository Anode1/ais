#!/bin/sh
# ais-web.command -- double-click on macOS to open the AIS web GUI.
#
# Finder runs a .command file on double-click. This finds the `ais` binary
# (on PATH, or beside this launcher, or in the repo) and runs `ais serve`,
# which starts a local server and opens your browser at 127.0.0.1:8765.
# A small Terminal window stays open while the server runs; close it to stop.

dir=$(cd "$(dirname "$0")" && pwd)
for cand in "$(command -v ais)" "$dir/ais" "$dir/../ais" "$dir/../c/ais"; do
    [ -n "$cand" ] && [ -x "$cand" ] && { exec "$cand" serve; }
done
echo "ais binary not found (build it with 'make', or put it next to this file)."
read -r _
