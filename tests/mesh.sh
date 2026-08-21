#!/bin/sh
# mesh.sh -- four devices, one index, many laps: does the mesh converge?
#
# tests/sync.sh pins the TRANSPORT between two peers. This pins the MERGE across
# a topology: phoneA -> pc1 -> phoneB -> pc2 -> phoneA, then pc1 <-> pc2, with
# records saved, edited, deleted, detached and re-attached BETWEEN the legs, so
# every fact has to travel through an intermediary rather than to a neighbour.
#
#     phoneA --- pc1 --- phoneB
#        \        |         |          --- LAN  (--sync --serve / --sync url)
#         \       |         | folder   === folder (--sync-folder)
#          ------ pc2 ======+
#
# The invariant, asserted after every lap: strip the device-local ids, resolve
# each document value to its BODY (blob names are device-local too), sort, and
# all four --dump outputs must be byte-identical. A record deleted anywhere is
# absent everywhere, and stays absent when a device that never saw the delete
# syncs later. Per-record spot checks miss exactly what a whole-mesh compare
# catches: a record that quietly exists twice, or with one tag too many.
#
# Exit 0 = passed, 1 = a failure, 77 = SKIP (no port to bind).
#
# Usage:  sh tests/mesh.sh [path-to-ais]      (default ./c/ais)

AIS=${1:-./c/ais}
case $AIS in
    /*) ;;
    *)  AIS=$(cd "$(dirname "$AIS")" && pwd)/$(basename "$AIS") ;;
esac
here=$(cd "$(dirname "$0")" && pwd)
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

W=$(mktemp -d "${TMPDIR:-/tmp}/ais_mesh.XXXXXX") || exit 2
host_pid=
cleanup() { [ -n "$host_pid" ] && kill "$host_pid" 2>/dev/null; rm -rf "$W"; }
trap cleanup EXIT INT TERM

PA="$W/phoneA"; P1="$W/pc1"; PB="$W/phoneB"; P2="$W/pc2"
FLD="$W/folder"                       # the shared folder phoneB and pc2 use
mkdir -p "$PA" "$P1" "$PB" "$P2" "$FLD"

# A port per leg, below the ephemeral range: repeatedly connecting to ONE port on
# loopback eventually re-picks a 4-tuple still in TIME_WAIT and the connect fails.
BASE=${AIS_MESH_PORT:-$((20000 + $$ % 9000))}
LEG=0
legs_retried=0
legs_lost=0

# ---------------------------------------------------------------- the legs ---

# lan HOSTDIR JOINDIR -- one two-way LAN round. The host serves one peer and exits.
lan() {
    LEG=$((LEG + 1)); port=$((BASE + LEG % 64))
    # Truncate the log HERE, in the parent: the child sets up its own redirect
    # only after the fork, so a first read can otherwise return the PREVIOUS
    # leg's token, and the join then fails on a token that looks perfectly valid.
    : > "$W/host.log"
    "$AIS" -f "$1" --sync --serve "$port" >"$W/host.log" 2>&1 &
    host_pid=$!
    i=0; tok=
    while [ $i -lt 60 ]; do            # a whole 32-hex token, never a part-line
        tok=$(sed -n 's/.*--token \([0-9a-f]\{32\}\)$/\1/p' "$W/host.log" 2>/dev/null | head -1)
        [ -n "$tok" ] && break
        kill -0 "$host_pid" 2>/dev/null || break
        i=$((i + 1)); sleep 0.25
    done
    [ -n "$tok" ] || { host_pid=; return 1; }
    # The host stays armed after a refused join (tests/sync.sh pins that), so a
    # transient connect failure is retried on the SAME token rather than costing
    # the suite the host's whole 120s timeout.
    rc=1; try=0
    while [ $try -lt 3 ]; do
        "$AIS" -f "$2" --sync "http://127.0.0.1:$port" --token "$tok" >>"$W/join.log" 2>&1
        rc=$?
        [ $rc -eq 0 ] && break
        try=$((try + 1)); legs_retried=$((legs_retried + 1)); sleep 0.5
    done
    [ $rc -eq 0 ] || { legs_lost=$((legs_lost + 1)); kill "$host_pid" 2>/dev/null; }
    wait "$host_pid" 2>/dev/null; host_pid=
    return $rc
}

# fold DIR_X DIR_Y -- a full two-way round through the shared folder. Three passes:
# X publishes, Y imports X and publishes, X imports Y.
fold() {
    "$AIS" -f "$1" --sync-folder "$FLD" >>"$W/fold.log" 2>&1
    "$AIS" -f "$2" --sync-folder "$FLD" >>"$W/fold.log" 2>&1
    "$AIS" -f "$1" --sync-folder "$FLD" >>"$W/fold.log" 2>&1
}

# one lap of the ring, in the owner's order
ring() { lan "$PA" "$P1"; lan "$P1" "$PB"; fold "$PB" "$P2"; lan "$P2" "$PA"; }

# settle -- laps until every fact has reached every device (a ring hop moves a
# fact one device per lap, so three laps cover a four-device ring), plus the
# pc1 <-> pc2 shortcut the owner asked for.
settle() { ring; lan "$P1" "$P2"; ring; ring; }

# ------------------------------------------------------------- the compare ---

# norm DIR -- the device's live state, device-local names removed:
#   * the id is already absent from --dump (it is device-local)
#   * a document value is replaced by a checksum of the BODY, because the blob
#     FILE NAME is minted locally and legitimately differs between devices
#   * the key set is sorted, since attach order is a local accident
norm() {
    d=$1
    "$AIS" -f "$d" --dump 2>/dev/null | while IFS= read -r line; do
        case $line in
            '-v '*)   keys=''      ; val=${line#-v } ;;
            *' -v '*) keys=${line%% -v *}; val=${line#* -v } ;;
            *)        keys='<none>'; val=$line ;;
        esac
        case $val in
            blobs/*)       val="doc[$(cksum < "$d/$val" 2>/dev/null || echo MISSING)]" ;;
            aisc:@blobs/*) val="edoc[$(cksum < "$d/${val#aisc:@}" 2>/dev/null || echo MISSING)]" ;;
        esac
        # deliberately unquoted: split the key field into words to sort them
        printf '%s| %s\n' "$(printf '%s\n' $keys | LC_ALL=C sort | tr '\n' ' ')" "$val"
    done | LC_ALL=C sort
}

# same LABEL DIR... -- every named device must hold the identical live state
same() {
    label=$1; shift
    first=; bad=
    for d in "$@"; do
        norm "$d" > "$W/n.$(basename "$d")"
        if [ -z "$first" ]; then first=$d
        elif ! cmp -s "$W/n.$(basename "$first")" "$W/n.$(basename "$d")"; then
            bad="$bad $(basename "$d")"
        fi
    done
    if [ -z "$bad" ]; then
        pass=$((pass + 1))
        echo "  ok   $label ($(grep -c . < "$W/n.$(basename "$first")") records on each)"
    else
        fail=$((fail + 1))
        echo "  FAIL $label -- diverged from $(basename "$first"):$bad"
        for d in $bad; do
            echo "       --- $(basename "$first") vs $d"
            diff "$W/n.$(basename "$first")" "$W/n.$d" | head -14 | sed 's/^/       /'
        done
    fi
}

# everywhere LABEL TEXT DIR... / nowhere LABEL TEXT DIR...
everywhere() {
    label=$1; want=$2; shift 2; miss=
    for d in "$@"; do
        "$AIS" -f "$d" --dump 2>/dev/null | grep -qF -- "$want" || miss="$miss $(basename "$d")"
    done
    if [ -z "$miss" ]; then pass=$((pass + 1)); echo "  ok   $label"
    else fail=$((fail + 1)); echo "  FAIL $label -- '$want' missing on:$miss"; fi
}
nowhere() {
    label=$1; want=$2; shift 2; had=
    for d in "$@"; do
        "$AIS" -f "$d" --dump 2>/dev/null | grep -qF -- "$want" && had="$had $(basename "$d")"
    done
    if [ -z "$had" ]; then pass=$((pass + 1)); echo "  ok   $label"
    else fail=$((fail + 1)); echo "  FAIL $label -- '$want' still on:$had"; fi
}

# id_of DIR VALUE -- the device-local id, which is what --del/--update/--add take
id_of() {
    "$AIS" -f "$1" --find "$2" 2>/dev/null | grep -F -- "|$2" | head -1 | cut -d'|' -f1
}

echo "mesh (four devices: phoneA, pc1, phoneB, pc2)"

# ------------------------------------------------------- lap 1: it all adds ---
#     Records appear on a different device before each leg, so each one has to
#     cross an intermediary to reach the far side of the ring.

"$AIS" -f "$PA" -v 'http://a/link-one'   alpha        >/dev/null
"$AIS" -f "$PA" -v 'a note with spaces'  alpha notes  >/dev/null
"$AIS" -f "$PA" -v 'значение'            ключ 日本語   >/dev/null
printf 'phoneA journal\nsecond line\n' | "$AIS" -f "$PA" --doc journal >/dev/null

lan "$PA" "$P1" || {
    echo "  SKIP could not bind a port near $BASE (set AIS_MESH_PORT to a free one)"
    [ "$fail" -eq 0 ] && exit 77 || exit 1
}
"$AIS" -f "$P1" -v 'http://p1/link-two'  beta   >/dev/null
"$AIS" -f "$P1" -v 'multi/first' -v 'multi/second' links >/dev/null

lan "$P1" "$PB"
"$AIS" -f "$PB" -v 'http://b/link-three' gamma  >/dev/null
sleep 1                                    # a distinct second: a distinct blob name
printf 'phoneB journal\n' | "$AIS" -f "$PB" --doc journal >/dev/null

fold "$PB" "$P2"
"$AIS" -f "$P2" -v 'http://p2/link-four' delta  >/dev/null

# an encrypted secret, made on pc2 over a real terminal (-e has no file bypass)
SECRET=
if command -v cc >/dev/null 2>&1 &&
   cc -std=c99 -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -o "$W/ptyrun" \
      "$here/pty/ptyrun.c" -lutil >/dev/null 2>&1; then
    printf 'hunter2-in-the-vault\npass phrase\npass phrase\n' > "$W/answers"
    eout=$("$W/ptyrun" "$W/answers" "$AIS" -f "$P2" vault -e 2>&1)
    case $eout in
        *"crypto not built"*) echo "  note: crypto not built -- the -e leg is not covered" ;;
        *) SECRET=$("$AIS" -f "$P2" --dump 2>/dev/null | grep -o 'aisc:[A-Za-z0-9+/=]*' | head -1) ;;
    esac
else
    echo "  note: no cc / no pty -- the -e leg is not covered"
fi
[ -n "$SECRET" ] || echo "  note: no encrypted record in this run"

lan "$P2" "$PA"
lan "$P1" "$P2"
settle

echo "  -- lap 1: everything added, nothing removed"
same      "lap1: all four devices hold the identical live state" "$PA" "$P1" "$PB" "$P2"
everywhere "lap1: a plain value crossed the whole ring"   'http://a/link-one'   "$PA" "$P1" "$PB" "$P2"
everywhere "lap1: a value with spaces kept its spaces"    'a note with spaces'  "$PA" "$P1" "$PB" "$P2"
everywhere "lap1: a UTF-8 keyed record arrived"           'значение'            "$PA" "$P1" "$PB" "$P2"
okeq      "lap1: and its UTF-8 keys still recall it (on pc2)" \
          "значение" "$("$AIS" -f "$P2" ключ 日本語 2>/dev/null | cut -d'|' -f2-)"
everywhere "lap1: a multi-link record's first value"      'multi/first'   "$PA" "$P1" "$PB" "$P2"
everywhere "lap1: and its second value"                   'multi/second'  "$PA" "$P1" "$PB" "$P2"
okeq      "lap1: the two links are ONE record on phoneB"  "1" \
          "$("$AIS" -f "$PB" links 2>/dev/null | cut -d'|' -f1 | sort -u | grep -c .)"
okeq      "lap1: both journal documents travelled" "2" \
          "$("$AIS" -f "$PA" --dump 2>/dev/null | grep -c '^journal -v blobs/')"
okeq      "lap1: pc1 holds both document BODIES"          "2" \
          "$(cat "$P1"/blobs/* 2>/dev/null | grep -c 'journal')"
if [ -n "$SECRET" ]; then
    everywhere "lap1: the encrypted secret arrived, still sealed" "$SECRET" "$PA" "$P1" "$PB" "$P2"
    nowhere    "lap1: and the cleartext never appeared" 'hunter2-in-the-vault' "$PA" "$P1" "$PB" "$P2"
    no         "lap1: nor anywhere in phoneA's store on disk" \
               'hunter2-in-the-vault' "$(cat "$PA/store" 2>/dev/null)"
fi

# --------------------------------- lap 2: deletes and a detach, phoneB lapsed --
#     phoneB takes no part in this lap. It is the device in a drawer, and it
#     still holds live what the other three have just deleted.

sleep 1
DEAD='http://p1/link-two'
"$AIS" -f "$PA" --del "$(id_of "$PA" "$DEAD")" -y >/dev/null 2>&1
NOTEID=$(id_of "$P1" 'a note with spaces')
"$AIS" -f "$P1" --update "$NOTEID" -- -alpha >/dev/null 2>&1   # detach one tag
"$AIS" -f "$P2" --untag delta -y >/dev/null 2>&1               # drop a tag everywhere

lan "$PA" "$P1"; lan "$P1" "$P2"; lan "$P2" "$PA"
lan "$PA" "$P1"; lan "$P1" "$P2"

echo "  -- lap 2: a delete, a tag detach and an --untag, with phoneB in a drawer"
same      "lap2: the three connected devices agree" "$PA" "$P1" "$P2"
nowhere   "lap2: the deleted record is gone on all three" "$DEAD" "$PA" "$P1" "$P2"
everywhere "lap2: while phoneB, unsynced, still holds it" "$DEAD" "$PB"
everywhere "lap2: the detached record itself is untouched" 'a note with spaces' "$PA" "$P1" "$P2"
okeq      "lap2: but 'alpha' no longer recalls it on pc2" "" \
          "$("$AIS" -f "$P2" alpha 2>/dev/null | grep 'note with spaces')"
okeq      "lap2: 'alpha' still recalls the record that kept it" \
          "http://a/link-one" "$("$AIS" -f "$P2" alpha 2>/dev/null | cut -d'|' -f2-)"
everywhere "lap2: --untag kept the record it untagged" 'http://p2/link-four' "$PA" "$P1" "$P2"
okeq      "lap2: and the tag itself is gone on phoneA" "" \
          "$("$AIS" -f "$PA" --keys 2>/dev/null | grep '^delta$')"

# ------------------------- lap 3: the lapsed device comes back with new work ---
#     It must NOT push back what the mesh deleted, and its own additions must
#     still arrive. This is the tombstone's whole job.

sleep 1
"$AIS" -f "$PB" --add "$(id_of "$PB" 'multi/first')" -v 'multi/third' >/dev/null 2>&1
"$AIS" -f "$PB" --update "$(id_of "$PB" 'http://a/link-one')" starred >/dev/null 2>&1
lan "$P1" "$PB"
fold "$PB" "$P2"
settle

echo "  -- lap 3: phoneB rejoins, holding a record the others deleted"
same      "lap3: all four agree again" "$PA" "$P1" "$PB" "$P2"
nowhere   "lap3: the stale peer did not resurrect the deleted record" "$DEAD" "$PA" "$P1" "$PB" "$P2"
everywhere "lap3: the third link phoneB added reached everyone" 'multi/third' "$PA" "$P1" "$PB" "$P2"
okeq      "lap3: as a link on the SAME record, not a new one (pc2)" "1" \
          "$("$AIS" -f "$P2" links 2>/dev/null | cut -d'|' -f1 | sort -u | grep -c .)"
okeq      "lap3: the tag phoneB attached recalls on pc2" \
          "http://a/link-one" "$("$AIS" -f "$P2" starred 2>/dev/null | cut -d'|' -f2-)"
okeq      "lap3: 'alpha' does not recall the detached note on phoneB" "" \
          "$("$AIS" -f "$PB" alpha 2>/dev/null | grep 'note with spaces')"

# ------------- lap 4: re-attach after a remote detach, a concurrent delete, ---
#                      and a compaction in the middle of the mesh

sleep 1
"$AIS" -f "$P2" --update "$(id_of "$P2" 'a note with spaces')" alpha >/dev/null 2>&1
GONE3='http://b/link-three'
"$AIS" -f "$PB" --del "$(id_of "$PB" "$GONE3")" -y >/dev/null 2>&1   # phoneB deletes ...
"$AIS" -f "$PA" --update "$(id_of "$PA" 'значение')" archived >/dev/null 2>&1  # ... as phoneA edits

lan "$P2" "$P1"
"$AIS" -f "$P1" --compact -y >/dev/null 2>&1        # a compaction mid-mesh
lan "$P1" "$PA"
settle

echo "  -- lap 4: a tag put back on, a delete beside an edit, and a --compact"
same      "lap4: all four agree after the compaction" "$PA" "$P1" "$PB" "$P2"
okeq      "lap4: the re-attached tag recalls the note on phoneB" \
          "a note with spaces" \
          "$("$AIS" -f "$PB" alpha 2>/dev/null | grep 'note with spaces' | cut -d'|' -f2-)"
everywhere "lap4: the re-attach reached every device" 'a note with spaces' "$PA" "$P1" "$PB" "$P2"
okeq      "lap4: and it is still ONE record with one value on pc1" "1" \
          "$("$AIS" -f "$P1" --dump 2>/dev/null | grep -c 'a note with spaces')"
nowhere   "lap4: the concurrent delete removed its record everywhere" "$GONE3" "$PA" "$P1" "$PB" "$P2"
everywhere "lap4: while the concurrent edit's record survived" 'значение' "$PA" "$P1" "$PB" "$P2"
okeq      "lap4: with the tag that edit added, on pc2" \
          "значение" "$("$AIS" -f "$P2" archived 2>/dev/null | cut -d'|' -f2-)"

# the compacted device must still be coherent: every key recalls exactly the
# live records --dump reports under it, and nothing the mesh deleted came back.
incoherent=
for d in "$PA" "$P1" "$PB" "$P2"; do
    "$AIS" -f "$d" --keys 2>/dev/null | while IFS= read -r k; do
        [ -n "$k" ] || continue
        g=$("$AIS" -f "$d" --dump 2>/dev/null | awk -v k="$k" '
              { keys = $0; sub(/ -v .*/, "", keys); v = $0; sub(/^.* -v /, "", v)
                if (index($0, " -v ") == 0) { keys = ""; sub(/^-v /, "", v) }
                n = split(keys, a, " ")
                for (i = 1; i <= n; i++) if (a[i] == k) print v }' | LC_ALL=C sort)
        if printf '%s\n' "$g" | grep -q '^blobs/'; then
            # a document recalls as its BODY, not as the blob path: count records
            rn=$("$AIS" -f "$d" "$k" 2>/dev/null | grep -c '^[0-9][0-9]*|')
            gn=$(printf '%s\n' "$g" | grep -c .)
            [ "$rn" = "$gn" ] || echo "$(basename "$d"):$k" >> "$W/incoherent"
        else
            r=$("$AIS" -f "$d" "$k" 2>/dev/null | cut -d'|' -f2- | LC_ALL=C sort)
            [ "$r" = "$g" ] || echo "$(basename "$d"):$k" >> "$W/incoherent"
        fi
    done
done
[ -f "$W/incoherent" ] && incoherent=$(head -5 "$W/incoherent" | tr '\n' ' ')
okeq "lap4: on every device, every key recalls exactly its live records" "" "$incoherent"

# one more lap must change nothing at all: sync is idempotent
for d in "$PA" "$P1" "$PB" "$P2"; do norm "$d" > "$W/before.$(basename "$d")"; done
settle
stable=
for d in "$PA" "$P1" "$PB" "$P2"; do
    norm "$d" > "$W/after.$(basename "$d")"
    cmp -s "$W/before.$(basename "$d")" "$W/after.$(basename "$d")" ||
        stable="$stable $(basename "$d")"
done
okeq "lap5: a further lap with no user action changes nothing" "" "$stable"
nowhere "lap5: and neither deleted record ever comes back" "$DEAD" "$PA" "$P1" "$PB" "$P2"
nowhere "lap5: nor the second one"                         "$GONE3" "$PA" "$P1" "$PB" "$P2"

# ----------------------------------------- two documents born the same second ---
#     Blob names are a timestamp, so two devices writing a doc in the same
#     second mint the SAME name for different bodies. Both bodies must survive,
#     and repeated syncing must not keep minting more of them.

DA="$W/docA"; DB="$W/docB"; DF="$W/docfolder"
mkdir -p "$DA" "$DB" "$DF"
printf 'body from A\n' | "$AIS" -f "$DA" --doc same >/dev/null
printf 'body from B\n' | "$AIS" -f "$DB" --doc same >/dev/null
if [ "$(ls "$DA/blobs")" = "$(ls "$DB/blobs")" ]; then
    "$AIS" -f "$DA" --sync-folder "$DF" >/dev/null 2>&1
    "$AIS" -f "$DB" --sync-folder "$DF" >/dev/null 2>&1
    "$AIS" -f "$DA" --sync-folder "$DF" >/dev/null 2>&1
    ok    "doc-clash: both bodies survive on A" "body from B" "$(cat "$DA"/blobs/* 2>/dev/null)"
    ok    "doc-clash: both bodies survive on B" "body from A" "$(cat "$DB"/blobs/* 2>/dev/null)"
    n1=$("$AIS" -f "$DA" --dump 2>/dev/null | grep -c .)
    for r in 1 2 3; do
        "$AIS" -f "$DB" --sync-folder "$DF" >/dev/null 2>&1
        "$AIS" -f "$DA" --sync-folder "$DF" >/dev/null 2>&1
    done
    n2=$("$AIS" -f "$DA" --dump 2>/dev/null | grep -c .)
    okeq  "doc-clash: further syncs do not mint more copies" "$n1" "$n2"
    same  "doc-clash: the two devices agree on the document set" "$DA" "$DB"
else
    echo "  note: the two documents landed in different seconds -- clash not exercised"
fi

[ "$legs_retried" -eq 0 ] || echo "  note: $legs_retried LAN join(s) had to be retried"
okeq "transport: every LAN leg completed" "0" "$legs_lost"

echo "  ---- mesh: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
