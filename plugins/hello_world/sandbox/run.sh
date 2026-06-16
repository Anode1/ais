#!/bin/sh
# run.sh -- isolated sandbox for hello_world. Builds a throwaway index, seeds
# fixtures, and runs the plugin against it; no real data is touched. POSIX sh.
set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
plugin=$(dirname -- "$here")
ais=${AIS:-ais}

idx=$(mktemp -d "${TMPDIR:-/tmp}/ais_hello.XXXXXX")
trap 'rm -rf "$idx"' EXIT
export AIS_INDEX="$idx"

# seed: each line of the fixtures file becomes a value under key 'demo'
# (-f points at the sandbox; the engine reads no env var for the index)
"$ais" -f "$idx" -v - demo < "$here/fixtures/notes.txt" >/dev/null

# run the plugin exactly as AIS would: index in $AIS_INDEX, exec the entry
# (the plugin passes it on with -f "$AIS_INDEX")
AIS="$ais" "$plugin/ais-hello" "$@"
