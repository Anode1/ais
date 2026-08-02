#!/bin/sh
# sync.sh -- two-way sync between two ais indexes over loopback, with no device,
# no emulator and no Flutter toolchain.
#
# Why this exists: `--sync --serve` and `--sync <url> --token` were driven by
# exactly one script, tests/gui/flutter-sync-android.sh, which SKIPs unless a
# phone is attached. So the headline feature of the release was never exercised
# by `make ut` on a developer machine or on CI -- covered on paper, unrun in
# practice. That is the same shape as the bug recorded in AGENTS.md, where a
# harness existed but was wired into nothing and sync stopped being tested for
# weeks. Two CLI peers on 127.0.0.1 need nothing but a socket, so this belongs in
# CORE and runs everywhere.
#
# It also pins --token, which had no assertion anywhere: a wrong token must be
# refused, or the pairing secret is decorative.
#
# Usage:  sh tests/sync.sh [path-to-ais]      (default ./c/ais)

AIS=${1:-./c/ais}
case $AIS in
    /*) ;;
    *)  AIS=$(cd "$(dirname "$AIS")" && pwd)/$(basename "$AIS") ;;
esac
pass=0
fail=0

ok() {
    if printf '%s' "$3" | grep -q -- "$2"; then
        pass=$((pass + 1)); echo "  ok   $1"
    else
        fail=$((fail + 1)); echo "  FAIL $1 -- expected '$2' in: [$3]"
    fi
}
no() {
    if printf '%s' "$3" | grep -q -- "$2"; then
        fail=$((fail + 1)); echo "  FAIL $1 -- '$2' must NOT appear in: [$3]"
    else
        pass=$((pass + 1)); echo "  ok   $1"
    fi
}
okeq() {
    if [ "$2" = "$3" ]; then pass=$((pass + 1)); echo "  ok   $1"
    else fail=$((fail + 1)); echo "  FAIL $1 -- expected '$2', got '$3'"; fi
}

W=$(mktemp -d "${TMPDIR:-/tmp}/ais_sync.XXXXXX") || exit 2
host_pid=
cleanup() { [ -n "$host_pid" ] && kill "$host_pid" 2>/dev/null; rm -rf "$W"; }
trap cleanup EXIT INT TERM

PORT=${AIS_SYNC_PORT:-8791}

# host_token PORT INDEX -- start a host in the background, echo its token (or nothing)
host_token() {
    "$AIS" -f "$2" --sync --serve "$1" >"$W/host.log" 2>&1 &
    host_pid=$!
    i=0
    while [ $i -lt 40 ]; do
        tok=$(grep -o 'token [0-9a-f]*' "$W/host.log" 2>/dev/null | awk '{print $2}')
        [ -n "$tok" ] && { printf '%s' "$tok"; return 0; }
        kill -0 "$host_pid" 2>/dev/null || return 1
        i=$((i + 1)); sleep 0.25
    done
    return 1
}

echo "sync (two CLI peers over loopback)"

# Two indexes, each holding one record the other has never seen.
"$AIS" -f "$W/a" -v 'http://from-a' atag >/dev/null 2>&1
"$AIS" -f "$W/b" -v 'http://from-b' btag >/dev/null 2>&1

TOK=$(host_token "$PORT" "$W/a") || {
    echo "  SKIP could not bind port $PORT (set AIS_SYNC_PORT to a free one)"
    [ "$fail" -eq 0 ] && exit 77 || exit 1
}
case $TOK in
    ????????????????????????????????) pass=$((pass+1)); echo "  ok   host: prints a 32-hex token" ;;
    *) fail=$((fail+1)); echo "  FAIL host: token is not 32 hex (got '$TOK')" ;;
esac

# A WRONG token must be refused. Nothing anywhere asserted this, so the pairing
# secret could have been ignored entirely and every test would still have passed.
bad=$("$AIS" -f "$W/b" --sync "http://127.0.0.1:$PORT" --token 00000000000000000000000000000000 2>&1); badrc=$?
no      "token: a wrong token does not merge"        "merged"      "$bad"
okeq    "token: and it fails loudly"                 "1"           "$badrc"
okempty_b=$("$AIS" -f "$W/b" atag 2>/dev/null)
okeq    "token: nothing from the host leaked in"     ""            "$okempty_b"

# The host is one-shot per round, so re-arm before the good join.
kill "$host_pid" 2>/dev/null; wait "$host_pid" 2>/dev/null
TOK=$(host_token "$PORT" "$W/a") || { echo "  FAIL host would not restart"; fail=$((fail+1)); }

if [ -n "$TOK" ]; then
    out=$("$AIS" -f "$W/b" --sync "http://127.0.0.1:$PORT" --token "$TOK" 2>&1)
    ok  "join: the round reports convergence"        "converged"   "$out"
    wait "$host_pid" 2>/dev/null; host_pid=

    # BOTH directions in one round is the contract, not just the pull.
    ok  "converge: b received a's record"            "http://from-a" "$("$AIS" -f "$W/b" atag 2>/dev/null)"
    ok  "converge: a received b's record"            "http://from-b" "$("$AIS" -f "$W/a" btag 2>/dev/null)"

    # A second identical round must add nothing: sync is idempotent, or repeated
    # pairing silently duplicates every record.
    before=$("$AIS" -f "$W/b" --dump 2>/dev/null | grep -c .)
    TOK2=$(host_token "$PORT" "$W/a")
    if [ -n "$TOK2" ]; then
        "$AIS" -f "$W/b" --sync "http://127.0.0.1:$PORT" --token "$TOK2" >/dev/null 2>&1
        wait "$host_pid" 2>/dev/null; host_pid=
        after=$("$AIS" -f "$W/b" --dump 2>/dev/null | grep -c .)
        okeq "idempotent: a second round adds nothing" "$before" "$after"
    fi
fi

echo "  ---- sync: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
