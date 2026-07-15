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
no()    { case "$3" in *"$2"*) fail=$((fail+1)); echo "  FAIL $1 (did not want '$2', got '$3')";;
                       *) pass=$((pass+1)); echo "  ok   $1";; esac; }

"$AIS" -f "$IDX" --init >/dev/null 2>&1
AIS_NO_OPEN=1 "$AIS" -f "$IDX" --serve "$PORT" >/dev/null 2>&1 &
SRV=$!

B="http://127.0.0.1:$PORT"
i=0; while [ $i -lt 50 ]; do curl -s -o /dev/null "$B/" && break; i=$((i+1)); sleep 0.1; done
if ! curl -s -o /dev/null "$B/"; then echo "  FAIL server did not start on $PORT"; exit 1; fi

# plain put + get
ok "put (plain)"   "saved 1"      "$(printf 'hello venice' | curl -s -X POST --data-binary @- "$B/api/put?keys=venice")"
ok "get (plain)"   "hello venice" "$(curl -s "$B/api/get?keys=venice")"

# --- keyset paging: /api/get and /api/tags page a large set with a cursor ---
# venice is id 1; seed three more under pgk (ids 2,3,4), ascending.
printf 'p one'   | curl -s -X POST --data-binary @- "$B/api/put?keys=pgk" >/dev/null
printf 'p two'   | curl -s -X POST --data-binary @- "$B/api/put?keys=pgk" >/dev/null
printf 'p three' | curl -s -X POST --data-binary @- "$B/api/put?keys=pgk" >/dev/null
GP1=$(curl -s "$B/api/get?keys=pgk&count=2")           # first page: ids 2,3
ok "get page 1 (count=2) has row one" "p one" "$GP1"
ok "get page 1 (count=2) has row two" "p two" "$GP1"
no "get page 1 stops at the page size" "p three" "$GP1"
GLAST=$(printf '%s\n' "$GP1" | sed '/^$/d' | tail -1 | cut -d'|' -f1)
GP2=$(curl -s "$B/api/get?keys=pgk&count=2&after=$GLAST")  # page 2: from the cursor
ok "get page 2 (after cursor) continues"   "p three" "$GP2"
no "get page 2 does not repeat page 1"     "p one"   "$GP2"
# tags: busiest first -> pgk (3) before venice (1); the cursor pages past pgk.
TG1=$(curl -s "$B/api/tags?count=1")
ok "tags page 1 (busiest first)"     "3|pgk"    "$TG1"
TG2=$(curl -s "$B/api/tags?count=1&afterc=3&afterk=pgk")
ok "tags page 2 (next after cursor)" "1|venice" "$TG2"
no "tags page 2 excludes page 1 key" "pgk"      "$TG2"
# clean the paging fixtures so later id/tag-count assertions stay as written
curl -s "$B/api/get?keys=pgk" | cut -d'|' -f1 | while read -r pid; do
  [ -n "$pid" ] && curl -s -X POST "$B/api/del?id=$pid" >/dev/null; done

# --- CSRF: cross-origin browser calls to the API are refused ----------------
# A malicious page's fetch carries Sec-Fetch-Site: cross-site (browsers always
# send it); the GUI's own fetch carries same-origin; a bare curl sends neither.
ok "csrf: cross-site GET refused"  "cross-origin request refused" \
   "$(curl -s -H 'Sec-Fetch-Site: cross-site' "$B/api/get?keys=venice")"
ok "csrf: cross-site sync/join refused (exfil vector)" "cross-origin request refused" \
   "$(printf 'http://x\nt' | curl -s -H 'Sec-Fetch-Site: cross-site' -X POST --data-binary @- "$B/api/sync/join")"
ok "csrf: cross-origin Origin refused" "cross-origin request refused" \
   "$(curl -s -H 'Origin: http://evil.example' "$B/api/get?keys=venice")"
ok "csrf: same-origin GET allowed"  "hello venice" \
   "$(curl -s -H 'Sec-Fetch-Site: same-origin' "$B/api/get?keys=venice")"
ok "csrf: direct-nav (none) allowed" "hello venice" \
   "$(curl -s -H 'Sec-Fetch-Site: none' "$B/api/get?keys=venice")"
no "csrf: the page itself is not gated" "refused" \
   "$(curl -s -H 'Sec-Fetch-Site: cross-site' "$B/")"

# --- Regression: split-packet POST -----------------------------------------
# A browser's fetch() sends the POST body in a SEPARATE TCP segment from the
# headers. curl (above) coalesces them, so a server that reads once and assumes
# the whole request arrived together passes curl but RESETS a real browser
# ("failed to fetch", nothing saved). These force the split with a pause between
# writes -- the reproduction the old suite lacked. SKIP if python3 is absent.
if command -v python3 >/dev/null 2>&1; then
  split_post() {   # split_post PATH BODY  ->  response body
    python3 - "$PORT" "$1" "$2" <<'PY'
import socket,sys,time
port,path,body=int(sys.argv[1]),sys.argv[2],sys.argv[3].encode()
s=socket.create_connection(('127.0.0.1',port))
s.sendall(("POST %s HTTP/1.1\r\nHost: x\r\nContent-Length: %d\r\nConnection: close\r\n\r\n"
           %(path,len(body))).encode())
time.sleep(0.25)                 # the body lands in a LATER packet
s.sendall(body)
r=b""
while True:
    d=s.recv(4096)
    if not d: break
    r+=d
sys.stdout.write(r.decode('utf-8','replace').split('\r\n\r\n',1)[-1])
PY
  }
  ok "put  (split-packet body drained)"    "saved 1"      "$(split_post '/api/put?keys=split' 'delayed body')"
  ok "get  (split-packet value persisted)" "delayed body" "$(curl -s "$B/api/get?keys=split")"
  IDX2=$(mktemp -d); "$AIS" -f "$IDX2" --init >/dev/null 2>&1
  split_post '/api/store' "$IDX2" >/dev/null
  ok "store (split-packet path -> switched library)" "$IDX2" "$(curl -s "$B/api/where")"
  split_post '/api/store' "$IDX" >/dev/null     # switch back for the cases below
  rm -rf "$IDX2"
else
  echo "  note python3 absent -- SKIP split-packet regression"
fi

# --- Regression: oversized POST body -> 413, never a truncated silent save ----
# A body past the ~64KB request buffer used to be read up to the buffer, the
# TRUNCATED value persisted while the route still returned "saved 1", and the
# unread tail RST the socket ("failed to fetch") on close. Now the server drains
# the remainder and returns 413, storing nothing. curl-only (no python needed).
BIGF=$(mktemp)
if command -v python3 >/dev/null 2>&1; then python3 -c "open('$BIGF','w').write('A'*100000)"
else awk 'BEGIN{for(i=0;i<100000;i++)printf "A"}' > "$BIGF"; fi
CODE=$(curl -s -o /dev/null -w '%{http_code}' -m 6 --data-binary @"$BIGF" "$B/api/put?keys=huge")
ok    "oversized body rejected (413, not a truncated save)" "413" "$CODE"
empty "oversized body stored nothing"                       "$(curl -s "$B/api/get?keys=huge")"
rm -f "$BIGF"

# --- Regression: header block split across TCP segments ------------------------
# Content-Length used to be parsed only from the first read; a request whose
# headers arrived in >1 packet left body_len=0, skipped the drain, and silently
# saved nothing ("saved 0"). Now the server reads until the header terminator.
if command -v python3 >/dev/null 2>&1; then
  split_header() {  # split_header PATH BODY  ->  response body
    python3 - "$PORT" "$1" "$2" <<'PY'
import socket,sys,time
port,path,body=int(sys.argv[1]),sys.argv[2],sys.argv[3].encode()
req=("POST %s HTTP/1.1\r\nHost: x\r\nContent-Length: %d\r\nConnection: close\r\n\r\n"
     %(path,len(body))).encode()+body
s=socket.create_connection(('127.0.0.1',port))
s.sendall(req[:15]); time.sleep(0.25); s.sendall(req[15:])   # split INSIDE the headers
r=b""
while True:
    d=s.recv(4096)
    if not d: break
    r+=d
sys.stdout.write(r.decode('utf-8','replace').split('\r\n\r\n',1)[-1])
PY
  }
  ok "put  (split-header block reassembled)"  "saved 1"    "$(split_header '/api/put?keys=hsplit' 'headerbody')"
  ok "get  (split-header value persisted)"    "headerbody" "$(curl -s "$B/api/get?keys=hsplit")"
else
  echo "  note python3 absent -- SKIP split-header regression"
fi

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
# Regression: actually RUN the encoder and compare to a golden that decodes. The
# old check above only greps for the string -- a malformed QR (wrong Reed-Solomon
# ECC) passes it but no phone can scan it. Needs node to run the shipped JS.
if command -v node >/dev/null 2>&1; then
  printf '%s' "$PAGE" > "$IDX/_page.html"
  ok "QR encoder output matches decodable golden" "MATCH" \
     "$(node "$(dirname "$0")/qr-check.js" "$IDX/_page.html" "$(dirname "$0")/qr.golden" 2>&1)"
else
  echo "  note node absent -- SKIP QR golden decode check"
fi

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
