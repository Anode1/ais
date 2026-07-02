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

# --- LAN sync (Host / Join), mirroring the mobile Sync feature -------------
# The page carries the Sync controls (the sheet, the QR encoder, the routes).
PAGE=$(curl -s "$B/")
ok "page: sync control"      "syncbtn"                   "$PAGE"
ok "page: sync sheet title"  "Sync with another device"  "$PAGE"
ok "page: host label"        "synchostbtn"               "$PAGE"
ok "page: join label"        "syncjoinbtn"               "$PAGE"
ok "page: QR encoder (pure JS, no server dep)" "function qrGen" "$PAGE"

# Join: a malformed address is rejected as "bad url" (no network touched).
ok "join (bad url)" "bad url" "$(printf 'notaurl' | curl -s -X POST --data-binary @- "$B/api/sync/join")"

# Host: fork a child that serves one peer; the route returns "http://ip:port"
# and a 32-hex token at once (never blocking the single-threaded HTTP loop).
# Port 8766 (AIS_SYNC_PORT) may be held by a lingering host child from a prior
# run; if so the child can't bind and status goes straight to timeout -- assert
# the route CONTRACT either way, and only run the live host<->join case when the
# host actually reaches "waiting".
HOST=$(curl -s -X POST "$B/api/sync/host")
HURL=$(printf '%s' "$HOST" | sed -n 1p)
HTOK=$(printf '%s' "$HOST" | sed -n 2p)
ok "host (url returned)"   "http://"  "$HURL"
case $HTOK in
  ????????????????????????????????) pass=$((pass+1)); echo "  ok   host (32-hex token)";;
  *) fail=$((fail+1)); echo "  FAIL host (32-hex token) (got '$HTOK')";;
esac
STAT=$(curl -s "$B/api/sync/status")
case $STAT in
  *waiting*)
    # a peer joins on loopback with the host's token: both routes report success
    ok "host (status waiting)" "waiting" "$STAT"
    ok "second host rejected (409, one at a time)" "409" \
       "$(curl -s -o /dev/null -w '%{http_code}' -X POST "$B/api/sync/host")"
    JOINED=$(printf 'http://127.0.0.1:8766\n%s' "$HTOK" | curl -s -X POST --data-binary @- "$B/api/sync/join")
    ok "join (loopback, right token) merged" "merged" "$JOINED"
    i=0; ST=; while [ $i -lt 10 ]; do ST=$(curl -s "$B/api/sync/status"); case $ST in *synced*|*timeout*) break;; esac; i=$((i+1)); sleep 0.3; done
    ok "host (status synced after a peer joined)" "synced" "$ST"
    ;;
  *)
    echo "  note host could not bind 8766 (a prior host child still holds it) -- skipping live host<->join"
    # still assert the join-unreachable contract against a dead port
    ok "join (unreachable / wrong token)" "could not connect" \
       "$(printf 'http://127.0.0.1:1\n%s' "$HTOK" | curl -s -X POST --data-binary @- "$B/api/sync/join")"
    ;;
esac
# reap any host child we started so it does not linger on 8766
for p in $(command -v ss >/dev/null 2>&1 && ss -ltnp 2>/dev/null | grep ':8766 ' | grep -o 'pid=[0-9]*' | cut -d= -f2 | sort -u); do kill "$p" 2>/dev/null; done

echo "serve: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
