#!/bin/sh
# run.sh -- the engine, the CLI and the web API on a 32-bit ABI.
#
# Every other layer runs on 64-bit Linux, where long, size_t and time_t are 64
# bits. An armeabi-v7a phone has all three at 32, and so does this container
# (i386 Debian): a date past 2038 cast into time_t came out as 1963 there and
# no test could see it. This copies the working tree, builds it inside the
# container and runs codeut, cliut and the web API suite.
#
# It does not run the Flutter app, so it cannot see a wrong Dart binding;
# tests/ffi-abi.sh rule 1 and the 32-bit emulator layer cover that.
#
# Exit 0 = pass, 1 = fail, 77 = SKIP (no Docker, or the image cannot be built).

root=$(cd "$(dirname "$0")/../.." && pwd)
here="$root/tests/abi32"
img=ais-abi32

command -v docker >/dev/null 2>&1 || { echo "  SKIP no docker"; exit 77; }
docker info >/dev/null 2>&1 || { echo "  SKIP docker is not running or not permitted"; exit 77; }
if ! docker image inspect "$img" >/dev/null 2>&1; then
    docker build -q -t "$img" "$here" >/dev/null 2>&1 ||
        { echo "  SKIP cannot build the $img image (network?)"; exit 77; }
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/ais_abi32.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT
# the working tree as it stands (tracked and untracked, not ignored): no objects
(cd "$root" && git ls-files -co --exclude-standard -z -- c tests Makefile man doc |
    tar --null -T - -cf - 2>/dev/null) | tar -xf - -C "$work" || exit 1

# Read-only mount, copied inside: the container's root then owns nothing on
# the host, and the tests that read the user's home find a passwd entry.
docker run --rm -v "$work":/in:ro "$img" sh -c '
    cp -a /in /src && cd /src || exit 1
    [ "$(getconf LONG_BIT)" = 32 ] || { echo "  FAIL the container is not 32-bit"; exit 1; }
    rc=0
    make >/tmp/build.log 2>&1 || { echo "  FAIL build"; tail -20 /tmp/build.log; exit 1; }
    make codeut >/tmp/ut.log 2>&1 || rc=1
    echo "  codeut: $(grep "passed," /tmp/ut.log | tail -1)"; grep "FAIL" /tmp/ut.log | head -20
    make cliut >/tmp/cli.log 2>&1 || rc=1
    echo "  cliut:  $(grep "passed," /tmp/cli.log | tail -1)"; grep "FAIL" /tmp/cli.log | head -20
    sh tests/gui/serve.sh /src/c/ais >/tmp/serve.log 2>&1; s=$?
    [ $s -eq 0 ] || [ $s -eq 77 ] || rc=1
    echo "  serve:  $(grep -i "passed" /tmp/serve.log | tail -1) (exit $s)"; grep "FAIL" /tmp/serve.log | head -20
    exit $rc'
