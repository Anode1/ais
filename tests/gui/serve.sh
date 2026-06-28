#!/bin/sh
# serve.sh -- GUI layer: end-to-end tests of the `ais --serve` HTTP API (the web
# GUI's backend), including the encrypt save + reveal round-trip. Starts the
# server HEADLESS (AIS_NO_OPEN=1) on a throwaway /tmp index and a high port,
# curls the endpoints, asserts, and tears it down. POSIX sh.
#
# Needs: the ais binary, curl, and the crypto module built (encrypt/reveal cases).
# Exit 0 = all passed, 1 = a failure, 77 = SKIP (curl absent).
#
# Usage:  sh tests/gui/serve.sh [path-to-ais]      (default ./c/ais)

AIS=${1:-./c/ais}
case $AIS in /*) ;; *) AIS=$(cd "$(dirname "$AIS")" && pwd)/$(basename "$AIS") ;; esac

command -v curl >/dev/null 2>&1 || { echo "serve: curl not found -- SKIP"; exit 77; }

IDX=$(mktemp -d)
PORT=$(( 18000 + ($$ % 2000) ))
SRV=
cleanup() { [ -n "$SRV" ] && kill "$SRV" 2>/dev/null; rm -rf "$IDX"; }
trap cleanup EXIT

pass=0; fail=0
ok()    { case "$3" in *"$2"*) pass=$((pass+1)); echo "  ok   $1";;
                       *) fail=$((fail+1)); echo "  FAIL $1 (want '$2', got '$3')";; esac; }
empty() { if [ -z "$2" ]; then pass=$((pass+1)); echo "  ok   $1";
          else fail=$((fail+1)); echo "  FAIL $1 (expected empty, got '$2')"; fi; }

"$AIS" -f "$IDX" --init >/dev/null 2>&1
AIS_NO_OPEN=1 "$AIS" -f "$IDX" --serve "$PORT" >/dev/null 2>&1 &
SRV=$!

B="http://127.0.0.1:$PORT"
i=0; while [ $i -lt 50 ]; do curl -s -o /dev/null "$B/" && break; i=$((i+1)); sleep 0.1; done
if ! curl -s -o /dev/null "$B/"; then echo "  FAIL server did not start on $PORT"; exit 1; fi

# plain put + get
ok "put (plain)"   "saved 1"      "$(printf 'hello venice' | curl -s -X POST --data-binary @- "$B/api/put?keys=venice")"
ok "get (plain)"   "hello venice" "$(curl -s "$B/api/get?keys=venice")"

# encrypt save: body is "passphrase\nvalue", ?enc=1
ok "put (encrypted)" "saved 1"    "$(printf 'pw123\ns3cr3t-token' | curl -s -X POST --data-binary @- "$B/api/put?keys=gmail&enc=1")"
MARKED=$(curl -s "$B/api/get?keys=gmail" | sed 's/^[0-9]*|//')
ok "stored opaque (aisc:)" "aisc:" "$MARKED"

# reveal round-trip
ok    "reveal (right passphrase)" "s3cr3t-token" "$(printf 'pw123\n%s' "$MARKED" | curl -s -X POST --data-binary @- "$B/api/reveal")"
empty "reveal (wrong passphrase fails closed)"   "$(printf 'nope\n%s'  "$MARKED" | curl -s -X POST --data-binary @- "$B/api/reveal")"

echo "serve: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
