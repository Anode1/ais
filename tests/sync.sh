#!/bin/sh
# sync.sh -- two-way sync between two ais indexes over loopback: no device, no
# emulator, no Flutter toolchain. Two CLI peers on 127.0.0.1 need only a socket,
# so this runs everywhere and belongs in CORE.
#
# It also pins --token: a wrong token must be refused, or the pairing secret is
# decorative.
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
    # Truncate in the PARENT: the child sets up its redirect only after the fork,
    # so a poll that starts first can scrape the PREVIOUS host's token out of the
    # stale log. The host then answers "wrong token" and, staying armed, sits out
    # its full 120s timeout while the assertion that needed the sync passes
    # vacuously. Match a whole 32-hex token for the same reason.
    : > "$W/host.log"
    "$AIS" -f "$2" --sync --serve "$1" >"$W/host.log" 2>&1 &
    host_pid=$!
    i=0
    while [ $i -lt 40 ]; do
        tok=$(grep -oE 'token [0-9a-f]{32}' "$W/host.log" 2>/dev/null | awk '{print $2}')
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

# A WRONG token must be refused.
bad=$("$AIS" -f "$W/b" --sync "http://127.0.0.1:$PORT" --token 00000000000000000000000000000000 2>&1); badrc=$?
no      "token: a wrong token does not merge"        "merged"      "$bad"
okeq    "token: and it fails loudly"                 "1"           "$badrc"
okempty_b=$("$AIS" -f "$W/b" atag 2>/dev/null)
okeq    "token: nothing from the host leaked in"     ""            "$okempty_b"

# ...and the mistake must not end the session. A 32-hex token is typed by hand
# from another device's screen, so the likeliest wrong token is a typo by the
# right person; ending the host would make them fetch a NEW one. The SAME host
# and the SAME token below: nothing was re-armed between these two joins.
if [ -n "$TOK" ]; then
    out=$("$AIS" -f "$W/b" --sync "http://127.0.0.1:$PORT" --token "$TOK" 2>&1)
    ok  "token: a typo does not end the session; the same token still joins" "converged" "$out"
    ok  "join: the round reports convergence"        "converged"   "$out"
    wait "$host_pid" 2>/dev/null; host_pid=

    # BOTH directions in one round is the contract, not just the pull.
    ok  "converge: b received a's record"            "http://from-a" "$("$AIS" -f "$W/b" atag 2>/dev/null)"
    ok  "converge: a received b's record"            "http://from-b" "$("$AIS" -f "$W/a" btag 2>/dev/null)"

    # A second identical round must add nothing: sync is idempotent.
    before=$("$AIS" -f "$W/b" --dump 2>/dev/null | grep -c .)
    TOK2=$(host_token "$PORT" "$W/a")
    if [ -n "$TOK2" ]; then
        "$AIS" -f "$W/b" --sync "http://127.0.0.1:$PORT" --token "$TOK2" >/dev/null 2>&1
        wait "$host_pid" 2>/dev/null; host_pid=
        after=$("$AIS" -f "$W/b" --dump 2>/dev/null | grep -c .)
        okeq "idempotent: a second round adds nothing" "$before" "$after"
    fi
fi

# --- an in-place --set reaches the peer ---------------------------------------
#     The store line keeps its id; what travels is a D| retiring the old value
#     and the new value as an ordinary record, so one round leaves the new text
#     alone on both sides, not both texts on both.
# --dump no longer carries an id (it is device-local; see doc/dev/FORMAT_V2.md).
# `get` still prints "id|value", which is where a --set/--del handle comes from.
sid=$("$AIS" -f "$W/b" btag 2>/dev/null | grep 'from-b' | cut -d'|' -f1)
if [ -n "$sid" ]; then
    sw=$("$AIS" -f "$W/b" --set "$sid" -v 'http://from-b' -v 'http://from-b-edited' 2>&1)
    no  "set: no warning, the edit propagates"      "does not propagate" "$sw"
    TOK3=$(host_token "$PORT" "$W/a")
    if [ -n "$TOK3" ]; then
        "$AIS" -f "$W/b" --sync "http://127.0.0.1:$PORT" --token "$TOK3" >/dev/null 2>&1
        wait "$host_pid" 2>/dev/null; host_pid=
        got=$("$AIS" -f "$W/a" btag 2>/dev/null)
        ok  "set: the peer received the edited value"   "from-b-edited" "$got"
        no  "set: and dropped the old one"               "|http://from-b$" "$got"
        okeq "set: one record on the peer, not two"      "1" "$(printf '%s\n' "$got" | grep -c .)"
    fi
fi

echo "  ---- sync: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
