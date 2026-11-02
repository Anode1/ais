#!/bin/sh
# due.sh -- the one-line "N due" hint, which exists only at a terminal.
#
# A recall or --timeline at a tty says "N due: ais --due" on stderr before its
# output, and a piped run says nothing new, so a script reading stdout is
# unchanged (tests/cli.sh pins the piped side). The tty side needs a terminal,
# so it is driven through tests/pty/ptyrun.c like the encrypt prompts.
#
# Exit 0 = passed, 1 = a failure, 77 = SKIP (no cc or no pty).
#
# Usage:  sh tests/pty/due.sh [path-to-ais]      (default ./c/ais)

AIS=${1:-./c/ais}
case $AIS in /*) ;; *) AIS=$(cd "$(dirname "$AIS")" && pwd)/$(basename "$AIS") ;; esac
here=$(cd "$(dirname "$0")" && pwd)

command -v cc >/dev/null 2>&1 || { echo "due: no cc -- SKIP"; exit 77; }

W=$(mktemp -d "${TMPDIR:-/tmp}/ais_ptydue.XXXXXX") || exit 2
trap 'rm -rf "$W"' EXIT INT TERM

RUN="$W/ptyrun"
cc -std=c99 -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -o "$RUN" "$here/ptyrun.c" -lutil 2>"$W/cc.log" || {
    echo "due: ptyrun would not build -- SKIP"; sed 's/^/      | /' "$W/cc.log" | head -5; exit 77; }

pass=0; fail=0
ok()    { case "$3" in *"$2"*) pass=$((pass+1)); echo "  ok   $1";;
                       *) fail=$((fail+1)); echo "  FAIL $1 (want '$2', got '$3')";; esac; }
no()    { case "$3" in *"$2"*) fail=$((fail+1)); echo "  FAIL $1 ('$2' must not appear in '$3')";;
                       *) pass=$((pass+1)); echo "  ok   $1";; esac; }

echo "due (the 'N due' hint at a real pty)"

: > "$W/answers"                                   # nothing to answer: no prompt
"$AIS" -f "$W/idx" --init >/dev/null
"$AIS" -f "$W/idx" -v "flush the tank" tank due:2020 >/dev/null
"$AIS" -f "$W/idx" -v "check the roof" roof due:2099 >/dev/null

out=$("$RUN" "$W/answers" "$AIS" -f "$W/idx" roof 2>&1); rc=$?
if [ $rc -eq 77 ]; then echo "due: no pty available -- SKIP"; exit 77; fi
ok "hint: a recall at a tty says how many are due"   "1 due: ais --due" "$out"
ok "hint: and still prints its records"             "check the roof"   "$out"
case $out in "1 due: ais --due"*) pass=$((pass+1)); echo "  ok   hint: the line comes first";;
             *) fail=$((fail+1)); echo "  FAIL hint: the line comes first (got '$out')";; esac

out=$("$RUN" "$W/answers" "$AIS" -f "$W/idx" --timeline 2>&1)
ok "hint: --timeline at a tty says it too"           "1 due: ais --due" "$out"

out=$("$RUN" "$W/answers" "$AIS" -f "$W/idx" --due 2>&1)
no "hint: --due itself carries no hint"              "due: ais --due"   "$out"
ok "hint: --due lists the due record"                "flush the tank"   "$out"

tid=$("$AIS" -f "$W/idx" tank | head -1 | cut -d'|' -f1)
"$AIS" -f "$W/idx" --update "$tid" -- -due:2020 >/dev/null
out=$("$RUN" "$W/answers" "$AIS" -f "$W/idx" roof 2>&1)
no "hint: nothing due, no line at all"               "due"              "$(printf '%s\n' "$out" | head -1 | grep -v roof)"


# the hint follows stderr alone: a piped stdout with a tty stderr still says it,
# a tty stdout with stderr sent to a file says nothing there
"$AIS" -f "$W/idx" -v "x" y due:2020 >/dev/null
out=$("$RUN" "$W/answers" sh -c "'$AIS' -f '$W/idx' roof | cat" 2>&1)
ok "hint: stdout piped, stderr at the tty: the line is said"  "1 due: ais --due" "$out"
"$RUN" "$W/answers" sh -c "'$AIS' -f '$W/idx' roof 2>'$W/err'" >/dev/null 2>&1
no "hint: stdout at the tty, stderr to a file: nothing there" "due"            "$(cat "$W/err")"
out=$("$RUN" "$W/answers" "$AIS" -f "$W/idx" nosuchkey 2>&1)
ok "hint: a recall that matches nothing still says it"       "1 due: ais --due" "$out"

echo "due: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
