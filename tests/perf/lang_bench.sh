#!/bin/sh
# lang_bench.sh -- C, Ada, Rust, Java, Python on ais's two core operations (full
# store scan, posting-list intersection), the SAME algorithm in each language.
# Builds the benches, regenerates the 1M-record index if absent, warms the page
# cache, runs all five. Result tables and analysis: LANG_COMPARISON.md.
set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
idx=${AIS_PERF_DIR:-/tmp/ais_1m}
n=${AIS_PERF_N:-1000000}
ais="$root/c/ais"

# the engine, used only to build the index
[ -x "$ais" ] || make -C "$root/c" >/dev/null

# generate once (gen.py is the slow part, ~2 min for 1M records)
if [ ! -f "$idx/store" ]; then
  echo "generating $n-record index in $idx (one time, ~2 min) ..."
  python3 "$here/gen.py" "$idx" "$n"
  "$ais" -f "$idx" -y --compact
fi

# two hottest keys (largest posting lists) for the intersection; any substring
# for the scan (scan time is substring-independent: it reads the whole store)
k1=$(find "$idx/idx" -type f -printf '%s %p\n' | sort -rn | sed -n 1p | cut -d' ' -f2-)
k2=$(find "$idx/idx" -type f -printf '%s %p\n' | sort -rn | sed -n 2p | cut -d' ' -f2-)
substr=${1:-Outputs}

# OPT=2 (default) or OPT=3: optimization level for the three native builds
opt=${OPT:-2}
cc "-O$opt" -o "$here/bench" "$here/bench.c"
javac -d "$here" "$here/Bench.java"
# Ada twice: checks on (the language default) and -gnatp (checks off, which is
# what the SPARK proof licenses). LANG_COMPARISON.md reports both.
ada=""; adap=""; command -v gnatmake >/dev/null 2>&1 && {
  mkdir -p "$here/obj_chk" "$here/obj_gnatp"   # gnatmake -D needs them to exist
  gnatmake "-O$opt" -D "$here/obj_chk" -o "$here/bench_ada" "$here/bench.adb" \
    >/dev/null 2>&1 && ada="$here/bench_ada"
  gnatmake "-O$opt" -gnatp -D "$here/obj_gnatp" -o "$here/bench_ada_gnatp" "$here/bench.adb" \
    >/dev/null 2>&1 && adap="$here/bench_ada_gnatp"; }
rust=""; command -v rustc >/dev/null 2>&1 && \
  rustc -C "opt-level=$opt" -C strip=symbols "$here/bench.rs" -o "$here/bench_rust" 2>/dev/null \
  && rust="$here/bench_rust"
cat "$idx/store" "$k1" "$k2" > /dev/null    # warm the cache: measure CPU, not disk

echo "### SCAN  (store, substring '$substr')   -O$opt ###"
echo "-- C --";      "$here/bench"  find "$idx/store" "$substr"
[ -n "$ada" ]  && { echo "-- Ada (checks on) --";  "$ada"  find "$idx/store" "$substr"; }
[ -n "$adap" ] && { echo "-- Ada (-gnatp) --";     "$adap" find "$idx/store" "$substr"; }
[ -n "$rust" ] && { echo "-- Rust --"; "$rust" find "$idx/store" "$substr"; }
echo "-- Java --";   java -cp "$here" Bench find "$idx/store" "$substr"
echo "-- Python --"; python3 "$here/bench.py" find "$idx/store" "$substr"
echo "### INTERSECTION  ($(basename "$k1")  AND  $(basename "$k2"))   -O$opt ###"
echo "-- C --";      "$here/bench"  and "$k1" "$k2"
[ -n "$ada" ]  && { echo "-- Ada (checks on) --";  "$ada"  and "$k1" "$k2"; }
[ -n "$adap" ] && { echo "-- Ada (-gnatp) --";     "$adap" and "$k1" "$k2"; }
[ -n "$rust" ] && { echo "-- Rust --"; "$rust" and "$k1" "$k2"; }
echo "-- Java --";   java -cp "$here" Bench and "$k1" "$k2"
echo "-- Python --"; python3 "$here/bench.py" and "$k1" "$k2"
