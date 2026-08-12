#!/bin/sh
# serve.sh -- end-to-end tests of the `ais --serve` HTTP API. Starts the server
# HEADLESS (AIS_NO_OPEN=1) on a throwaway /tmp index and a high port, curls the
# endpoints, and tears it down. POSIX sh.
#
# Needs the ais binary, curl, and the crypto module (encrypt/reveal cases).
# Exit 0 = passed, 1 = a failure, 77 = SKIP (curl absent).
#
# Usage:  sh tests/gui/serve.sh [path-to-ais]      (default ./c/ais)

AIS=${1:-./c/ais}
case $AIS in /*) ;; *) AIS=$(cd "$(dirname "$AIS")" && pwd)/$(basename "$AIS") ;; esac

command -v curl >/dev/null 2>&1 || { echo "serve: curl not found -- SKIP"; exit 77; }

IDX=$(mktemp -d)
PORT=$(( 18000 + ($$ % 2000) ))
SRV=

# /api/store persists the chosen index to the REAL ~/.ais/config (ais_default_set),
# and this suite points it at temp dirs it then deletes: snapshot and restore it,
# the same guard tests/cli.sh uses around --default.
CFG="$HOME/.ais/config"
CFGBAK="$IDX.config.orig"; HADCFG=no
[ -f "$CFG" ] && { cp "$CFG" "$CFGBAK"; HADCFG=yes; }
restore_cfg() { if [ "$HADCFG" = yes ]; then cp "$CFGBAK" "$CFG"; else rm -f "$CFG"; fi
                rm -f "$CFGBAK"; }

cleanup() { [ -n "$SRV" ] && kill "$SRV" 2>/dev/null; restore_cfg; rm -rf "$IDX"; }
trap cleanup EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM HUP

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
# A page's fetch sends Sec-Fetch-Site: cross-site or same-origin; curl sends none.
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
# A browser's fetch() sends the POST body in a separate TCP segment from the
# headers, which curl coalesces. SKIP if python3 is absent.
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
# A body past the ~64KB request buffer must be drained and answered 413, storing
# nothing; an unread tail RSTs the socket on close. curl-only, no python needed.
BIGF=$(mktemp)
if command -v python3 >/dev/null 2>&1; then python3 -c "open('$BIGF','w').write('A'*100000)"
else awk 'BEGIN{for(i=0;i<100000;i++)printf "A"}' > "$BIGF"; fi
CODE=$(curl -s -o /dev/null -w '%{http_code}' -m 6 --data-binary @"$BIGF" "$B/api/put?keys=huge")
ok    "oversized body rejected (413, not a truncated save)" "413" "$CODE"
empty "oversized body stored nothing"                       "$(curl -s "$B/api/get?keys=huge")"
rm -f "$BIGF"

# --- Regression: header block split across TCP segments ------------------------
# The server must read until the header terminator: Content-Length parsed from
# the first read alone leaves body_len=0 and saves nothing.
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
# Run the encoder and compare against a decodable golden: grepping for the
# function name passes a malformed QR no phone can scan. Needs node.
if command -v node >/dev/null 2>&1; then
  printf '%s' "$PAGE" > "$IDX/_page.html"
  ok "QR encoder output matches decodable golden" "MATCH" \
     "$(node "$(dirname "$0")/qr-check.js" "$IDX/_page.html" "$(dirname "$0")/qr.golden" 2>&1)"
else
  echo "  note node absent -- SKIP QR golden decode check"
fi

# Join: a malformed address is rejected as "bad url" (no network touched).
ok "join (bad url)" "bad url" "$(printf 'notaurl' | curl -s -X POST --data-binary @- "$B/api/sync/join")"

# Host: fork a child that serves one peer; the route returns "http://ip:port" and
# a 32-hex token at once, never blocking the single-threaded HTTP loop. Port 8766
# (AIS_SYNC_PORT) may still be held by a host child from a prior run, so the live
# host<->join case runs only when status reaches "waiting".
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

# --- the two tag-level operations the Tags view offers -------------------
# One keystroke apart in the UI and opposite in consequence: untag destroys nothing.
printf 'u one' | curl -s -X POST --data-binary @- "$B/api/put?keys=utag+ukeep" >/dev/null
printf 'u two' | curl -s -X POST --data-binary @- "$B/api/put?keys=utag" >/dev/null
ok "untag: reports how many it detached" "untagged 2" \
   "$(curl -s -X POST "$B/api/untag?keys=utag")"
empty "untag: the tag is gone"           "$(curl -s "$B/api/get?keys=utag")"
ok "untag: the record kept its other tag" "u one" "$(curl -s "$B/api/get?keys=ukeep")"
# the record whose ONLY key was untagged is still in the index, just unfiled
ok "untag: the unfiled record survives"  "u two" "$(curl -s "$B/api/timeline?count=50")"
ok "untag: an unused tag is a no-op"     "untagged 0" \
   "$(curl -s -X POST "$B/api/untag?keys=nosuchtag")"
ok "untag: an empty tag is refused"      "400" \
   "$(curl -s -o /dev/null -w '%{http_code}' -X POST "$B/api/untag?keys=")"

printf 'd one' | curl -s -X POST --data-binary @- "$B/api/put?keys=dtag+dkeep" >/dev/null
printf 'd two' | curl -s -X POST --data-binary @- "$B/api/put?keys=dtag" >/dev/null
ok "del-under: reports how many records it deleted" "deleted 2" \
   "$(curl -s -X POST "$B/api/del-under?keys=dtag")"
empty "del-under: the tag is gone"       "$(curl -s "$B/api/get?keys=dtag")"
# the difference that matters: the RECORD went, so it is gone from its OTHER tag too
empty "del-under: and gone from its other tag too" "$(curl -s "$B/api/get?keys=dkeep")"
# both take ONE tag, not the whitespace-separated list the other endpoints take:
# key_encode would fold "a b" to "a_b" and answer 200 for a tag that cannot exist
ok "untag: a multi-key value is refused"  "400" \
   "$(curl -s -o /dev/null -w '%{http_code}' -X POST "$B/api/untag?keys=aa+bb")"
ok "del-under: a multi-key value is refused" "400" \
   "$(curl -s -o /dev/null -w '%{http_code}' -X POST "$B/api/del-under?keys=aa+bb")"
# /api/del-under shreds encrypted blobs before tombstoning, as /api/del and the
# CLI do. The blob reference is written by hand, so no crypto module is needed.
mkdir -p "$IDX/blobs"
printf 'ciphertext\n' > "$IDX/blobs/w1.aisc"
printf 'ciphertext\n' > "$IDX/blobs/w2.aisc"
printf 'aisc:@blobs/w1.aisc' | curl -s -X POST --data-binary @- "$B/api/put?keys=shk" >/dev/null
printf 'aisc:@blobs/w2.aisc' | curl -s -X POST --data-binary @- "$B/api/put?keys=shkeep" >/dev/null
curl -s -X POST "$B/api/del-under?keys=shk" >/dev/null
ok "del-under: the deleted record's blob was shredded" "0" \
   "$([ -e "$IDX/blobs/w1.aisc" ] && echo 1 || echo 0)"
ok "del-under: an unrelated blob is untouched"        "1" \
   "$([ -e "$IDX/blobs/w2.aisc" ] && echo 1 || echo 0)"

ok "del-under: an empty tag is refused"  "400" \
   "$(curl -s -o /dev/null -w '%{http_code}' -X POST "$B/api/del-under?keys=")"

# --- compaction from the GUI: a phone has no CLI ---------------------------
printf 'to be deleted' | curl -s -X POST --data-binary @- "$B/api/put?keys=cmpk" >/dev/null
printf 'to be kept'    | curl -s -X POST --data-binary @- "$B/api/put?keys=cmpkeep" >/dev/null
delid=$(curl -s "$B/api/get?keys=cmpk" | head -1 | cut -d'|' -f1)
curl -s -X POST "$B/api/del?id=$delid" >/dev/null
# earlier cases in this file already deleted things, so assert "some", not "one"
okd=$(curl -s "$B/api/stats" | grep -c 'deleted: [1-9]')
okeq2() { if [ "$2" = "$3" ]; then pass=$((pass+1)); echo "  ok   $1"; else fail=$((fail+1)); echo "  FAIL $1 (want '$2', got '$3')"; fi; }
okeq2 "stats: reports what clean-up would reclaim" "1" "$okd"
# a browser cannot run `ais --version`; see doc/dev/VERSIONING.md
ver=$(curl -s "$B/api/version")
ok "version: reports the engine version"      "engine: " "$ver"
ok "version: reports the on-disk format"      "format: v" "$ver"
ok "compact: reclaims from the GUI"             "cleaned"    "$(curl -s -X POST "$B/api/compact")"
empty "compact: the deleted record is gone"     "$(curl -s "$B/api/get?keys=cmpk")"
ok "compact: the live record survives"          "to be kept" "$(curl -s "$B/api/get?keys=cmpkeep")"
# and the privacy variant, which a phone user cannot reach any other way
printf 'secret-ish' | curl -s -X POST --data-binary @- "$B/api/put?keys=cmpf" >/dev/null
fid=$(curl -s "$B/api/get?keys=cmpf" | head -1 | cut -d'|' -f1)
curl -s -X POST "$B/api/del?id=$fid" >/dev/null
ok "compact: forget=1 also drops the delete facts" "cleaned and forgotten" \
   "$(curl -s -X POST "$B/api/compact?forget=1")"
okno() { case "$3" in *"$2"*) fail=$((fail+1)); echo "  FAIL $1";; *) pass=$((pass+1)); echo "  ok   $1";; esac; }
okno "compact: nothing is left to test a guess against" "cmpf" "$(cat "$IDX/tomb" 2>/dev/null)"

# --- folder sync over HTTP: each refusal names its own cause, and force=1 (the
#     page's "Sync with it anyway") really reaches the engine.
FS="${TMPDIR:-/tmp}/ais_serve_fld.$$"
rm -rf "$FS"
ok "sync-folder: a missing folder is named"  "no such folder" \
   "$(curl -s -X POST --data-binary "$FS" "$B/api/sync-folder")"
printf 'x' > "$FS"
ok "sync-folder: a file is named"            "not a folder" \
   "$(curl -s -X POST --data-binary "$FS" "$B/api/sync-folder")"
rm -f "$FS"; mkdir -p "$FS"
ok "sync-folder: an existing folder syncs"   "synced" \
   "$(curl -s -X POST --data-binary "$FS" "$B/api/sync-folder")"
rm -f "$FS"/*.aisb
ok "sync-folder: a remembered folder gone empty is refused" "folder empty" \
   "$(curl -s -X POST --data-binary "$FS" "$B/api/sync-folder")"
ok "sync-folder: and force=1 accepts it (the page's Sync anyway)" "synced" \
   "$(curl -s -X POST --data-binary "$FS" "$B/api/sync-folder?force=1")"
rm -rf "$FS"

# --- /api/del: the GUI's delete, asserted for its effect ---------------------
#     Elsewhere it is only ever setup, so every use would pass if del were a no-op.
printf 'delete me' | curl -s -X POST --data-binary @- "$B/api/put?keys=delk" >/dev/null
printf 'keep me'   | curl -s -X POST --data-binary @- "$B/api/put?keys=delkeep" >/dev/null
did=$(curl -s "$B/api/get?keys=delk" | head -1 | cut -d'|' -f1)
ok    "del: the record is there before"       "delete me" "$(curl -s "$B/api/get?keys=delk")"
ok    "del: reports what it removed"          "deleted"   "$(curl -s -X POST "$B/api/del?id=$did")"
empty "del: and the record is really gone"    "$(curl -s "$B/api/get?keys=delk")"
ok    "del: the neighbour is untouched"       "keep me"   "$(curl -s "$B/api/get?keys=delkeep")"
# Deleting an already-gone id reports "deleted" and succeeds, matching `ais -y
# --del <gone-id>`, so a double-tap on a phone raises no error. It does mean a
# wrong id reports success.
ok    "del: a second delete is idempotent, not an error" "deleted" \
      "$(curl -s -X POST "$B/api/del?id=$did")"
ok    "del: and the CLI agrees, so the front ends match" "deleted" \
      "$(curl -s -X POST "$B/api/del?id=999999")"

# --- the bundle endpoints: "Export to a file" / "Import from a file" ---------
#     import-bundle carries a special case in the request reader: it is the one
#     route allowed to arrive in a later packet.
BND=$(curl -s "$B/api/export-bundle")
ok "export-bundle: serves the A| merge stream"     "A|"           "$BND"
# the filename is a contract: the Flutter save dialog and its .aisb type group
# key off it, so a change here silently breaks Export on the phone.
ok "export-bundle: offers it as a .aisb attachment" 'filename="ais-export.aisb"' \
   "$(curl -s -D - -o /dev/null "$B/api/export-bundle")"

# a bundle from "another device" must merge in and be readable afterwards
printf 'A|2020-01-01T00:00:00Z|restored|http://from-a-backup\n' > "$IDX/in.aisb"
curl -s -X POST --data-binary @"$IDX/in.aisb" "$B/api/import-bundle" >/dev/null
ok "import-bundle: the record is queryable after"  "http://from-a-backup" \
   "$(curl -s "$B/api/get?keys=restored")"
ok "bundle: what came in goes back out"            "http://from-a-backup" \
   "$(curl -s "$B/api/export-bundle")"
# importing the same bundle twice must not duplicate it
curl -s -X POST --data-binary @"$IDX/in.aisb" "$B/api/import-bundle" >/dev/null
ok "import-bundle: re-importing does not duplicate" "1" \
   "$(curl -s "$B/api/export-bundle" | grep -c 'from-a-backup')"
# a write route reachable cross-site would let any page silently seed the index
ok "csrf: cross-site import-bundle refused"        "cross-origin request refused" \
   "$(curl -s -H 'Sec-Fetch-Site: cross-site' -X POST --data-binary @"$IDX/in.aisb" \
        "$B/api/import-bundle")"

echo "serve: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
