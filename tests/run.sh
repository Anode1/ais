#!/bin/sh
# run.sh -- the ais test suite, in two clearly separated groups:
#
#   CORE  engine unit tests (c/tests.c via `make ut`) + CLI black-box (cli.sh).
#         The commit gate -- keep it green. (`make check` runs exactly this.)
#   GUI   the front-ends over the one engine: --serve HTTP, native Windows,
#         Flutter. May lag while a GUI is in progress; a layer whose toolchain is
#         absent (no MinGW, no Flutter SDK) reports SKIP, not FAIL.
#
# A green CORE with a red or skipped GUI is fine to commit -- the summary says
# which side. Exit is non-zero iff a non-skipped layer FAILS.
#
# Usage:  sh tests/run.sh        or        make suite

root=$(cd "$(dirname "$0")/.." && pwd)
AIS="$root/c/ais"
fail_core=0; fail_gui=0; skips=0

bar() { echo "============================================================"; }
sub() { echo "------------------------------------------------------------"; }

layer() {  # layer FAILVAR LABEL CMD...
    var=$1; label=$2; shift 2
    out=$("$@" 2>&1); rc=$?
    case $rc in
        0)  v=PASS ;;
        77) v=SKIP; skips=$((skips + 1)) ;;
        *)  v=FAIL; eval "$var=1" ;;
    esac
    printf '  %-26s %s\n' "$label" "$v"
    [ $rc -eq 0 ] || [ $rc -eq 77 ] || echo "$out" | sed 's/^/      | /'
}

bar; echo "ais test suite"; bar
make -C "$root/c" >/dev/null 2>&1 || { echo "build FAILED -- aborting"; exit 1; }

echo "CORE  (engine + CLI -- the commit gate; keep green)"
sub
layer fail_core "engine unit (make ut)" make -C "$root/c" ut
layer fail_core "cli black-box"         sh "$root/tests/cli.sh" "$AIS"

echo "GUI  (one engine, many front-ends; absent toolchain = SKIP)"
sub
layer fail_gui  "serve http api"        sh "$root/tests/gui/serve.sh" "$AIS"
layer fail_gui  "native windows ui"     sh "$root/tests/gui/windows.sh"
layer fail_gui  "flutter app"           sh "$root/tests/gui/flutter.sh"

bar
cs=$([ $fail_core -eq 0 ] && echo PASS || echo FAIL)
gs=$([ $fail_gui  -eq 0 ] && echo PASS || echo FAIL)
printf 'CORE: %s    GUI: %s    (skipped: %d)\n' "$cs" "$gs" "$skips"
bar
[ $fail_core -eq 0 ] && [ $fail_gui -eq 0 ]
