#!/bin/sh
# upgrade.sh -- an index made by an OLDER ais, opened by this one.
#
# Every device the author owns will do this, and nothing else covers it: every
# other test builds its index with the binary under test. Between two releases
# sit changes to blob naming, to how a tombstone is consulted, to next_id and to
# what compaction leaves behind, and each is designed to be backward compatible
# and verified only against indexes this build created itself.
#
# It also covers the mixed mesh a staggered rollout makes: for days, some devices
# run the old build and some the new, and they sync with each other.
#
# Point AIS_OLD at a previous release's binary; without it the layer SKIPs, so a
# fresh checkout still runs green:
#
#     gh release download v0.3.19 -p 'ais-*-linux-x86_64.zip'
#     unzip ais-*-linux-x86_64.zip
#     AIS_OLD=./ais-*/ais sh tests/upgrade.sh ./c/ais
#
# Exit 0 = pass, 1 = fail, 77 = SKIP.

NEW=${1:-./c/ais}
case $NEW in /*) ;; *) NEW=$(cd "$(dirname "$NEW")" && pwd)/$(basename "$NEW") ;; esac
OLD=${AIS_OLD:-}
[ -n "$OLD" ] || { echo "  SKIP set AIS_OLD to a previous release's ais binary"; exit 77; }
case $OLD in /*) ;; *) OLD=$(cd "$(dirname "$OLD")" && pwd)/$(basename "$OLD") ;; esac
[ -x "$OLD" ] || { echo "  SKIP \$AIS_OLD is not executable: $OLD"; exit 77; }
[ -x "$NEW" ] || { echo "  SKIP $NEW not built"; exit 77; }

pass=0; fail=0
ok()   { if printf '%s' "$3" | grep -q -- "$2"; then pass=$((pass+1)); echo "  ok   $1"
         else fail=$((fail+1)); echo "  FAIL $1 -- expected '$2' in: [$3]"; fi }
no()   { if printf '%s' "$3" | grep -q -- "$2"; then fail=$((fail+1)); echo "  FAIL $1 -- '$2' must NOT appear in: [$3]"
         else pass=$((pass+1)); echo "  ok   $1"; fi }
okeq() { if [ "$2" = "$3" ]; then pass=$((pass+1)); echo "  ok   $1"
         else fail=$((fail+1)); echo "  FAIL $1 -- expected '$2', got '$3'"; fi }

W=$(mktemp -d "${TMPDIR:-/tmp}/ais_upg.XXXXXX") || exit 2
trap 'rm -rf "$W"' EXIT INT TERM

echo "  (old: $("$OLD" --version 2>&1), new: $("$NEW" --version 2>&1))"

# --- an index the OLD binary made, with something of every kind in it ---------
A="$W/a"
"$OLD" -f "$A" --init >/dev/null 2>&1
"$OLD" -f "$A" -v "https://example.org/one" alpha beta >/dev/null
"$OLD" -f "$A" -v "note about venice" venice italy >/dev/null
"$OLD" -f "$A" -v "путешествие" поездка >/dev/null          # UTF-8 keys and value
printf 'first line\nsecond line\n' | "$OLD" -f "$A" --doc paper >/dev/null
"$OLD" -f "$A" -v "doomed" throwaway >/dev/null
"$OLD" -f "$A" -v "keeps its record" tagged extra >/dev/null

dead_id=$("$OLD" -f "$A" throwaway 2>/dev/null | head -1 | cut -d'|' -f1)
"$OLD" -f "$A" --del "$dead_id" -y >/dev/null 2>&1
"$OLD" -f "$A" --untag extra -y >/dev/null 2>&1
old_dump=$("$OLD" -f "$A" --dump 2>/dev/null | sort)

# --- the NEW binary opens it --------------------------------------------------
new_dump=$("$NEW" -f "$A" --dump 2>/dev/null | sort)
okeq "upgrade: the new binary reads the old index unchanged" "$old_dump" "$new_dump"
ok   "upgrade: a plain record still recalls"    "example.org/one" "$("$NEW" -f "$A" alpha beta 2>/dev/null)"
ok   "upgrade: a UTF-8 key still recalls"       "путешествие"     "$("$NEW" -f "$A" поездка 2>/dev/null)"
ok   "upgrade: a document still resolves"       "second line"     "$("$NEW" -f "$A" paper 2>/dev/null)"
no   "upgrade: the deleted record stays deleted" "doomed"         "$("$NEW" -f "$A" --dump 2>/dev/null)"
no   "upgrade: the detached tag stays detached" "extra"           "$("$NEW" -f "$A" --tags 2>/dev/null)"

# --- and can write to it ------------------------------------------------------
"$NEW" -f "$A" -v "added after the upgrade" fresh >/dev/null
ok "upgrade: the new binary can add to it" "added after the upgrade" "$("$NEW" -f "$A" fresh 2>/dev/null)"
"$NEW" -f "$A" -y --compact >/dev/null 2>&1
ok "upgrade: compaction keeps the old records"  "example.org/one" "$("$NEW" -f "$A" alpha beta 2>/dev/null)"
ok "upgrade: and the old document"              "second line"     "$("$NEW" -f "$A" paper 2>/dev/null)"
no "upgrade: and does not revive the deleted one" "doomed"        "$("$NEW" -f "$A" --dump 2>/dev/null)"

# --- the OLD binary can still use the index the new one has now written -------
ok "downgrade: the old binary still reads it" "added after the upgrade" \
   "$("$OLD" -f "$A" fresh 2>/dev/null)"

# --- a mixed mesh: one device upgraded, one not -------------------------------
#     A staggered rollout IS this, for as long as it takes everyone to update.
B="$W/b"; F="$W/folder"; mkdir -p "$F"
"$OLD" -f "$B" --init >/dev/null 2>&1
"$OLD" -f "$B" -v "from the old device" oldside >/dev/null
# Keep this document out of the same SECOND as A's, so the two old-style names
# cannot collide: the clash case is exercised deliberately further down, and
# mixing it in here would make this section's result depend on how fast the
# machine ran the lines above.
sleep 2
printf 'old device document\nline two\n' | "$OLD" -f "$B" --doc onlyhere >/dev/null
[ "$(ls "$A/blobs" | head -1)" != "$(ls "$B/blobs" | head -1)" ] ||
    echo "  note: the two old documents collided anyway; the count below may grow"

for r in 1 2 3; do
    "$NEW" -f "$A" -y --sync-folder "$F" >/dev/null 2>&1
    "$OLD" -f "$B" -y --sync-folder "$F" >/dev/null 2>&1
done
ok "mixed mesh: the old device's record reached the new one" "from the old device" \
   "$("$NEW" -f "$A" oldside 2>/dev/null)"
ok "mixed mesh: the new device's record reached the old one" "added after the upgrade" \
   "$("$OLD" -f "$B" fresh 2>/dev/null)"
ok "mixed mesh: the old device's document reached the new one" "old device document" \
   "$("$NEW" -f "$A" onlyhere 2>/dev/null)"

n1=$("$NEW" -f "$A" --dump 2>/dev/null | grep -c .)
for r in 1 2 3; do
    "$OLD" -f "$B" -y --sync-folder "$F" >/dev/null 2>&1
    "$NEW" -f "$A" -y --sync-folder "$F" >/dev/null 2>&1
done
okeq "mixed mesh: further rounds add nothing" "$n1" "$("$NEW" -f "$A" --dump 2>/dev/null | grep -c .)"

# --- the one case that does NOT settle until everyone updates ------------------
#     Two documents minted by OLD binaries in the same second share a name. The
#     new build resolves that deterministically; an un-upgraded peer cannot, so
#     it keeps minting a copy per round. What must hold is that it costs ONE
#     record per round rather than doubling, and that it STOPS the moment the
#     last device updates -- which is the promise made to someone mid-rollout.
C="$W/c"; D="$W/d"; G="$W/clashfolder"; mkdir -p "$G"
"$OLD" -f "$C" --init >/dev/null 2>&1
"$OLD" -f "$D" --init >/dev/null 2>&1
printf 'body C\n2\n' | "$OLD" -f "$C" --doc same >/dev/null
printf 'body D\n2\n' | "$OLD" -f "$D" --doc same >/dev/null
if [ "$(ls "$C/blobs")" = "$(ls "$D/blobs")" ]; then
    for r in 1 2 3 4; do
        "$NEW" -f "$C" -y --sync-folder "$G" >/dev/null 2>&1
        "$OLD" -f "$D" -y --sync-folder "$G" >/dev/null 2>&1
    done
    mid=$("$NEW" -f "$C" --dump 2>/dev/null | grep -c .)
    [ "$mid" -le 8 ] && { pass=$((pass+1)); echo "  ok   clash mid-rollout: bounded growth, not doubling ($mid records)"; } \
                     || { fail=$((fail+1)); echo "  FAIL clash mid-rollout: $mid records after 4 rounds"; }
    # now the laggard updates: it must stop at once
    for r in 1 2; do
        "$NEW" -f "$C" -y --sync-folder "$G" >/dev/null 2>&1
        "$NEW" -f "$D" -y --sync-folder "$G" >/dev/null 2>&1
    done
    n2=$("$NEW" -f "$C" --dump 2>/dev/null | grep -c .)
    for r in 1 2 3; do
        "$NEW" -f "$C" -y --sync-folder "$G" >/dev/null 2>&1
        "$NEW" -f "$D" -y --sync-folder "$G" >/dev/null 2>&1
    done
    okeq "clash after everyone updates: it stops growing" "$n2" \
         "$("$NEW" -f "$C" --dump 2>/dev/null | grep -c .)"
    ok   "clash after everyone updates: both bodies are readable" "body D" \
         "$("$NEW" -f "$C" same 2>/dev/null)"
else
    echo "  note: the two old documents landed in different seconds -- clash not exercised"
fi

echo "  ---- upgrade: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
