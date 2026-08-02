#!/bin/sh
# encrypt.sh -- the CLI `-e` path, end to end, over a real pseudo-terminal.
#
# `ais KEY -e` prompts for the secret and the passphrase on /dev/tty with echo
# off, and has NO environment or file bypass. That is deliberate: $AIS_TTY is
# visible in `ps -e` and /proc/PID/environ, and an answers file is a file, so
# routing a passphrase through either would leak it to any other user on the
# machine and into any test script that used it. The consequence was that the
# whole CLI encrypt path had never been driven: c/tests.c covers aisc_encrypt at
# the C API and the aisc: marker parsing, and nothing joined them.
#
# So we give it a terminal instead of a bypass (tests/pty/ptyrun.c) and assert
# the security property itself: with $AIS_TTY set and no controlling terminal,
# -e must FAIL rather than read the passphrase from the file.
#
# Exit 0 = passed, 1 = a failure, 77 = SKIP (no cc, no pty, or crypto not built).
#
# Usage:  sh tests/pty/encrypt.sh [path-to-ais]      (default ./c/ais)

AIS=${1:-./c/ais}
case $AIS in /*) ;; *) AIS=$(cd "$(dirname "$AIS")" && pwd)/$(basename "$AIS") ;; esac
here=$(cd "$(dirname "$0")" && pwd)

command -v cc >/dev/null 2>&1 || { echo "encrypt: no cc -- SKIP"; exit 77; }

W=$(mktemp -d "${TMPDIR:-/tmp}/ais_pty.XXXXXX") || exit 2
trap 'rm -rf "$W"' EXIT INT TERM

RUN="$W/ptyrun"
cc -std=c99 -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -o "$RUN" "$here/ptyrun.c" -lutil 2>"$W/cc.log" || {
    echo "encrypt: ptyrun would not build -- SKIP"; sed 's/^/      | /' "$W/cc.log" | head -5; exit 77; }

pass=0; fail=0
ok()    { case "$3" in *"$2"*) pass=$((pass+1)); echo "  ok   $1";;
                       *) fail=$((fail+1)); echo "  FAIL $1 (want '$2', got '$3')";; esac; }
no()    { case "$3" in *"$2"*) fail=$((fail+1)); echo "  FAIL $1 ('$2' must not appear in '$3')";;
                       *) pass=$((pass+1)); echo "  ok   $1";; esac; }

echo "encrypt (-e over a real pty)"

# secret value, then passphrase, then the confirmation of the passphrase
mkdir -p "$W/idx" "$W/idx2"
printf 'my-secret-value\ncorrect horse\ncorrect horse\n' > "$W/answers"
out=$("$RUN" "$W/answers" "$AIS" -f "$W/idx" mysecret -e 2>&1)
rc=$?
if [ $rc -eq 77 ]; then echo "encrypt: no pty available -- SKIP"; exit 77; fi
case $out in
    *"crypto not built"*) echo "encrypt: crypto module not built -- SKIP"; exit 77 ;;
esac

ok "e: the prompt asks for the secret off the terminal"  "secret value" "$out"
ok "e: and for a passphrase"                             "passphrase"   "$out"
# the cleartext must never be echoed back: ECHO is cleared for exactly this
no "e: the secret is not echoed to the terminal"         "my-secret-value" "$out"

stored=$(cat "$W/idx/store" 2>/dev/null)
ok "e: the record is stored under the aisc: marker"      "aisc:"        "$stored"
no "e: the cleartext never reaches the store"            "my-secret-value" "$stored"

# recall through a pipe (not a terminal) must stay opaque, per `ais --help`
piped=$("$AIS" -f "$W/idx" mysecret 2>/dev/null | cat)
ok "e: a piped recall stays the opaque aisc: value"      "aisc:"        "$piped"
no "e: a piped recall does not reveal the secret"        "my-secret-value" "$piped"

# THE SECURITY PROPERTY, pinned. With no controlling terminal, -e must fail --
# it must NOT fall back to $AIS_TTY, which is world-readable in ps and /proc.
# This is the regression test for a "fix" that would make -e scriptable.
if command -v setsid >/dev/null 2>&1; then
    printf 'leaked-value\nleaked-pass\nleaked-pass\n' > "$W/leak"
    lo=$(AIS_TTY="$W/leak" setsid "$AIS" -f "$W/idx2" leaktest -e </dev/null 2>&1)
    no "e: AIS_TTY does not supply the passphrase"       "leaked-value" "$(cat "$W/idx2/store" 2>/dev/null)"
    ok "e: without a terminal it fails instead"          "-e" "$lo"
else
    echo "  SKIP setsid absent -- cannot test the no-terminal case"
fi

echo "encrypt: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
