#!/bin/sh
# mcpb.sh -- build the MCP Bundle (.mcpb) from the per-platform release zips.
#   sh scripts/mcpb.sh VERSION OUTDIR ZIP...
# Each ZIP is a dist.sh binary bundle named ais-*-<os>-<arch>.zip; its ais lands
# in server/<os>-<arch>/, beside mcpb/ais-mcp, which picks one at run time.
# Writes OUTDIR/ais-vVERSION.mcpb and its .sha256. The registry entry
# (server.json) is made from the published file by scripts/registry.sh.
set -e
[ $# -ge 3 ] || { echo "usage: mcpb.sh VERSION OUTDIR ZIP..." >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=$1; out=$(CDPATH= cd -- "$2" && pwd); shift 2
stage=$(mktemp -d); trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/server"

sed "s/@VERSION@/$version/" "$root/mcpb/manifest.json" > "$stage/manifest.json"
cp "$root/mcpb/ais-mcp" "$stage/server/ais-mcp"
cp "$root/icons/ais-512.png" "$stage/icon.png"
cp "$root/COPYING" "$root/LICENSE-MIT" "$stage/"

for z in "$@"; do
    plat=$(basename "$z" .zip | sed 's/.*-\([a-z]*-[a-z0-9_]*\)$/\1/')
    mkdir -p "$stage/server/$plat"
    unzip -p "$z" '*/ais' > "$stage/server/$plat/ais"
    [ -s "$stage/server/$plat/ais" ] || { echo "mcpb: no ais in $z" >&2; exit 1; }
done

find "$stage" -type d -exec chmod 0755 {} +
find "$stage" -type f -exec chmod 0644 {} +
chmod 0755 "$stage/server/ais-mcp" "$stage"/server/*/ais

name="ais-v$version.mcpb"
rm -f "$out/$name"
( cd "$stage" && zip -rqX "$out/$name" . )
( cd "$out" && shasum -a 256 "$name" > "$name.sha256" )
echo "built $out/$name"
