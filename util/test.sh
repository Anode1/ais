#!/bin/sh
# test.sh -- known-answer check for the exif reader. Run by `make check`.
# mktest emits a JPEG with a fixed EXIF block; we assert exif decodes it, and
# that a JPEG with no EXIF reports nothing.
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exif=$here/exif
mktest=$here/mktest
[ -x "$exif" ] && [ -x "$mktest" ] || { echo "build first: (cd $here && make check)" >&2; exit 2; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
pass=0; fail=0

ck()  { if printf '%s' "$3" | grep -q -- "$2"; then pass=$((pass+1)); echo "  ok   $1"
        else fail=$((fail+1)); echo "  FAIL $1 -- want '$2' in [$3]"; fi; }
cke() { if [ "$2" -eq "$3" ]; then pass=$((pass+1)); echo "  ok   $1"
        else fail=$((fail+1)); echo "  FAIL $1 -- want exit $2, got $3"; fi; }

"$mktest" > "$tmp/known.jpg"
out=$("$exif" "$tmp/known.jpg"); rc=$?
cke "exit 0 when EXIF is present"        0 "$rc"
ck  "DateTimeOriginal wins over DateTime" "2023-07-14"          "$out"
ck  "GPS rationals become decimals"       "45.440833 12.315556" "$out"

printf '\377\330\377\331' > "$tmp/none.jpg"      # SOI + EOI, no EXIF
"$exif" "$tmp/none.jpg" >/dev/null 2>&1; rc=$?
cke "exit 1 when there is no EXIF"       1 "$rc"

echo "---- $pass passed, $fail failed"
[ "$fail" -eq 0 ]
