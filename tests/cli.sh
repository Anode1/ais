#!/bin/sh
# cli.sh -- end-to-end tests of the ais BINARY.
#
# These reach what the C unit tests (which call the ais.h API directly) cannot:
# the streaming stdin path (`-v -`), real pipelines, argv handling, and that an
# inserted key is genuinely present in the index afterwards. POSIX sh, so it
# runs unchanged on Linux and macOS.
#
# Grammar under test (flag-based): bare args are KEYS; -v marks a value;
# --word is a command. See c/help.c / doc/dev or `ais --help`.
#
# Usage:  sh tests/cli.sh [path-to-ais]      (default ./c/ais)

AIS=${1:-./c/ais}
# Resolve to an absolute path: several tests cd into temp dirs to exercise
# index discovery/relativization, where a relative ./c/ais would not resolve.
case $AIS in
    /*) ;;
    *)  AIS=$(cd "$(dirname "$AIS")" && pwd)/$(basename "$AIS") ;;
esac
pass=0
fail=0

# `timeout` guards the untag cases so a lost loop-guard FAILS instead of hanging
# the suite. macOS does not ship it (it is gtimeout from coreutils), where it was
# "timeout: command not found" -> exit 127 -> 18 spurious failures on the release
# runner. Run unguarded there: no guard is worse than a red suite, far better than
# a broken one.
if command -v timeout >/dev/null 2>&1; then AIS_TO="timeout"
elif command -v gtimeout >/dev/null 2>&1; then AIS_TO="gtimeout"
else AIS_TO=""; fi
tmo() { if [ -n "$AIS_TO" ]; then "$AIS_TO" "$@"; else shift; "$@"; fi; }

# ok LABEL EXPECTED ACTUAL  -- pass if EXPECTED is a substring of ACTUAL
ok() {
    if printf '%s' "$3" | grep -q -- "$2"; then
        pass=$((pass + 1)); echo "  ok   $1"
    else
        fail=$((fail + 1)); echo "  FAIL $1 -- expected '$2' in: [$3]"
    fi
}

# okempty LABEL ACTUAL  -- pass if ACTUAL is empty
okempty() {
    if [ -z "$2" ]; then
        pass=$((pass + 1)); echo "  ok   $1"
    else
        fail=$((fail + 1)); echo "  FAIL $1 -- expected empty, got: [$2]"
    fi
}

# okeq LABEL EXPECTED ACTUAL  -- pass if the two strings are equal
okeq() {
    if [ "$2" = "$3" ]; then
        pass=$((pass + 1)); echo "  ok   $1"
    else
        fail=$((fail + 1)); echo "  FAIL $1 -- expected '$2', got '$3'"
    fi
}

DIR=$(mktemp -d "${TMPDIR:-/tmp}/ais_cli.XXXXXX") || exit 2
trap 'rm -rf "$DIR"' EXIT

echo "ais CLI / streaming tests ($AIS)"

# 1. stdin streaming: three piped lines become three records under one key
printf 'alpha\nbeta\ngamma\n' | "$AIS" -f "$DIR" -v - greek
out=$("$AIS" -f "$DIR" greek)
ok    "stdin: first piped line stored"  "alpha" "$out"
ok    "stdin: last piped line stored"   "gamma" "$out"
n=$(printf '%s\n' "$out" | grep -c .)
okeq  "stdin: exactly 3 records"        "3" "$n"

# ... and the key itself is now present in the index
keys=$("$AIS" -f "$DIR" --keys)
ok    "stdin: key 'greek' is in the database" "greek" "$keys"

# 2. self-indexing round-trip: pipe the ais binary's own path in, then get it
#    back by key AND find it by content -- two checks in one pipeline.
find "$AIS" | "$AIS" -f "$DIR" -v - executable
out=$("$AIS" -f "$DIR" executable)
ok    "self-index: binary path stored under 'executable'" "ais" "$out"
out=$("$AIS" -f "$DIR" --find ais)
ok    "self-index: '--find ais' locates the path"         "ais" "$out"
keys=$("$AIS" -f "$DIR" --keys)
ok    "self-index: key 'executable' is in the database"   "executable" "$keys"

# 3. find is content (the value), not tags (the key)
printf 'venice is sinking\n' | "$AIS" -f "$DIR" -v - trip
out=$("$AIS" -f "$DIR" --find venice)
ok      "find: matches a value substring"     "venice is sinking" "$out"
out=$("$AIS" -f "$DIR" --find nosuchword)
okempty "find: absent term prints nothing"    "$out"

# 4. git-style location: `--init` creates a local .ais, found from subdirectories
TREE=$(mktemp -d "${TMPDIR:-/tmp}/ais_tree.XXXXXX") || exit 2
( cd "$TREE" && "$AIS" --init >/dev/null )
if [ -d "$TREE/.ais" ]; then
    pass=$((pass + 1)); echo "  ok   init: creates .ais in the current dir"
else
    fail=$((fail + 1)); echo "  FAIL init: no .ais created"
fi
mkdir -p "$TREE/sub/deep"
( cd "$TREE/sub/deep" && printf 'a note\n' | "$AIS" -v - memo >/dev/null )
out=$(cd "$TREE" && "$AIS" memo)
ok "discovery: a put from a subdir walked up to .ais" "a note" "$out"
keys=$(cd "$TREE/sub" && "$AIS" --keys)
ok "discovery: key visible from another subdir"       "memo"   "$keys"
rm -rf "$TREE"

# 5. immutability: --del needs confirmation; no input aborts, -y bypasses
DD=$(mktemp -d "${TMPDIR:-/tmp}/ais_del.XXXXXX") || exit 2
id=$("$AIS" -f "$DD" -v "scratch value" tmp)
"$AIS" -f "$DD" --del "$id" </dev/null >/dev/null 2>&1     # no -y, EOF -> aborted
out=$("$AIS" -f "$DD" tmp)
ok      "guard: del without confirmation is refused" "scratch value" "$out"
"$AIS" -f "$DD" -y --del "$id" >/dev/null                  # -y -> deletes
out=$("$AIS" -f "$DD" tmp)
okempty "guard: del -y removes the record" "$out"
rm -rf "$DD"

# 6. interactive: stdin = values, keys read per line from $AIS_TTY (scripted tty)
II=$(mktemp -d "${TMPDIR:-/tmp}/ais_i.XXXXXX") || exit 2
printf 'x1\nx2\n' > "$II/keys"
printf 'http://a\nhttp://b\n' | AIS_TTY="$II/keys" "$AIS" -f "$II/idx" -i kul >/dev/null 2>&1
out=$("$AIS" -f "$II/idx" kul)
ok "interactive: base key applied to all values"  "http://a" "$out"
ok "interactive: base key applied to all (2)"     "http://b" "$out"
out=$("$AIS" -f "$II/idx" x1)
ok "interactive: per-line key x1 -> first value"  "http://a" "$out"
case "$out" in
    *http://b*) fail=$((fail + 1)); echo "  FAIL interactive: x1 leaked to second value" ;;
    *)          pass=$((pass + 1)); echo "  ok   interactive: x1 not on the second value" ;;
esac
rm -rf "$II"

# 7. import: keys|value lines; same keys recall together; round-trips dump
IM=$(mktemp -d "${TMPDIR:-/tmp}/ais_import.XXXXXX") || exit 2
printf 'a b|first\na b|second\nc|third\n# comment\n\nnobar-skip\n' | "$AIS" -f "$IM" --import 2>/dev/null
out=$("$AIS" -f "$IM" a b)
ok "import: same-keys recall both"     "first"  "$out"
ok "import: same-keys recall both (2)" "second" "$out"
out=$("$AIS" -f "$IM" c)
ok "import: distinct-key record"       "third"  "$out"
# dump (strip id) | import into a fresh index reproduces the values
IM2=$(mktemp -d "${TMPDIR:-/tmp}/ais_import2.XXXXXX") || exit 2
"$AIS" -f "$IM" --dump | sed 's/^[0-9]*|//' | "$AIS" -f "$IM2" --import 2>/dev/null
out=$("$AIS" -f "$IM2" a b)
ok "import: dump|import round-trips"    "second" "$out"
rm -rf "$IM" "$IM2"

# 8. doc: a multi-line document becomes a blob file; recall CATS its content
#    (the blob is content, not a reference -- the stored path is resolved on read)
DC=$(mktemp -d "${TMPDIR:-/tmp}/ais_doc.XXXXXX") || exit 2
printf 'line one\nline two\nline three\n' | "$AIS" -f "$DC" --doc kul memo >/dev/null 2>&1
out=$("$AIS" -f "$DC" kul memo)
ok "doc: recall cats the blob content, not the blobs/ path"  'line three' "$out"
blob=$(ls "$DC"/blobs/*.txt 2>/dev/null | head -1)
if [ -n "$blob" ] && [ -f "$blob" ]; then
    pass=$((pass + 1)); echo "  ok   doc: blob file created"
else
    fail=$((fail + 1)); echo "  FAIL doc: blob file missing"
fi
okeq "doc: blob preserved 3 lines"   "3" "$(( $(wc -l < "$blob") ))"   # $(()) strips BSD wc's leading pad
ok "where: prints the index dir"     "$DC" "$("$AIS" -f "$DC" --where)"
rm -rf "$DC"

# 9. multi-link: two -v under one key make one record (id) with two values
ML=$(mktemp -d "${TMPDIR:-/tmp}/ais_ml.XXXXXX") || exit 2
mlid=$("$AIS" -f "$ML" -v linkA -v linkB project)
out=$("$AIS" -f "$ML" project)
ok "multi-link: first value present"  "linkA" "$out"
ok "multi-link: second value present" "linkB" "$out"
n=$(printf '%s\n' "$out" | grep -c .)
okeq "multi-link: both under one record" "2" "$n"
rm -rf "$ML"

# 10. keyless capture: -v with no key stores a value, found by --find / --dump
KL=$(mktemp -d "${TMPDIR:-/tmp}/ais_kl.XXXXXX") || exit 2
"$AIS" -f "$KL" -v "call Marina back" >/dev/null
out=$("$AIS" -f "$KL" --find Marina)
ok "keyless: stored value is findable" "call Marina back" "$out"
rm -rf "$KL"

# 11. default project key: set it, every put gets it; -p '' resets
PJ=$(mktemp -d "${TMPDIR:-/tmp}/ais_proj.XXXXXX") || exit 2
"$AIS" -f "$PJ" --project kul >/dev/null
"$AIS" -f "$PJ" -v "deploy-cmd" deploy >/dev/null
ok "project: put auto-tagged with default 'kul'" "deploy-cmd" "$("$AIS" -f "$PJ" kul)"
ok "project: also under the explicit key"        "deploy-cmd" "$("$AIS" -f "$PJ" deploy)"
"$AIS" -f "$PJ" -p '' -v "global note" misc >/dev/null
case "$("$AIS" -f "$PJ" kul)" in
    *"global note"*) fail=$((fail + 1)); echo "  FAIL project: -p '' leaked into kul" ;;
    *)               pass=$((pass + 1)); echo "  ok   project: -p '' resets (not under kul)" ;;
esac
ok "project: show the default"                   "kul"    "$("$AIS" -f "$PJ" --project)"
rm -rf "$PJ"

# 12. --add attaches another value to an existing record; --stats summarizes
AD=$(mktemp -d "${TMPDIR:-/tmp}/ais_add.XXXXXX") || exit 2
aid=$("$AIS" -f "$AD" -v firstlink note)
"$AIS" -f "$AD" --add "$aid" -v secondlink >/dev/null
out=$("$AIS" -f "$AD" note)
ok "add: original value still present"     "firstlink"  "$out"
ok "add: added value attached to record"   "secondlink" "$out"
"$AIS" -f "$AD" -v third other >/dev/null
"$AIS" -f "$AD" -y --del 2 >/dev/null
st=$("$AIS" -f "$AD" --stats)
# assert the NUMBERS, not merely that something was printed: 1 record live (the
# two links count as one), 1 deleted, and 2 keys still filed until compaction.
ok "stats: counts the live records" "records: 1$" "$st"
ok "stats: counts the deleted"      "deleted: 1$" "$st"
ok "stats: counts the keys"         "keys: 2$"    "$st"
rm -rf "$AD"

# 13. --del-key tombstones every record under a key (-y skips the prompt)
DK=$(mktemp -d "${TMPDIR:-/tmp}/ais_dk.XXXXXX") || exit 2
"$AIS" -f "$DK" -v a1 gone >/dev/null
"$AIS" -f "$DK" -v a2 gone >/dev/null
"$AIS" -f "$DK" -y --del-key gone >/dev/null
okempty "del-key: all records under the key removed" "$("$AIS" -f "$DK" gone)"
rm -rf "$DK"

# 14. --compact reclaims space: a deleted record physically leaves the store
CP=$(mktemp -d "${TMPDIR:-/tmp}/ais_cp.XXXXXX") || exit 2
cid=$("$AIS" -f "$CP" -v doomed scratch)
"$AIS" -f "$CP" -v survivor scratch >/dev/null   # without this, a store wiped to
"$AIS" -f "$CP" -y --del "$cid" >/dev/null       # nothing also passed the test
"$AIS" -f "$CP" -y --compact >/dev/null
ok "compact: the live record is still there" "survivor" "$(cat "$CP"/store)"
ok "compact: and still answers by key"       "survivor" "$("$AIS" -f "$CP" scratch)"
case "$(cat "$CP"/store)" in
    *doomed*) fail=$((fail + 1)); echo "  FAIL compact: deleted value still in store" ;;
    *)        pass=$((pass + 1)); echo "  ok   compact: deleted record physically gone" ;;
esac
rm -rf "$CP"

# 15. concurrency: two parallel writers never collide on an id (per-op write
#     lock + fresh next_id). Regression for the reader/writer lock change.
CC=$(mktemp -d "${TMPDIR:-/tmp}/ais_cc.XXXXXX") || exit 2
( i=0; while [ $i -lt 50 ]; do "$AIS" -f "$CC" -v "a$i" w1 >/dev/null; i=$((i+1)); done ) &
( i=0; while [ $i -lt 50 ]; do "$AIS" -f "$CC" -v "b$i" w2 >/dev/null; i=$((i+1)); done ) &
wait
total=$(grep -c . "$CC"/store)
uniq=$(cut -d'|' -f1 "$CC"/store | sort -un | grep -c .)
okeq "concurrency: 100 records written"            "100" "$total"
okeq "concurrency: all ids unique (no collision)"  "$total" "$uniq"
rm -rf "$CC"

# 16. --timeline (newest first; a dateless/hand-edited record shown first, not
#     lost) and --tags (every key with its count, busiest first)
TT=$(mktemp -d "${TMPDIR:-/tmp}/ais_tl.XXXXXX") || exit 2
"$AIS" -f "$TT" -v "https://a.example" alpha shared >/dev/null
"$AIS" -f "$TT" -v "https://b.example" beta shared  >/dev/null
"$AIS" -f "$TT" -v "a plain note"      gamma         >/dev/null
printf '99|legacy hand|pasted with no date\n' >> "$TT"/store   # legacy v1 line
rm -f "$TT"/next_id "$TT"/off "$TT"/multi
"$AIS" -f "$TT" --compact -y >/dev/null                        # reindex from store
tags=$("$AIS" -f "$TT" --tags)
ok    "tags: the shared key is listed"          "shared"    "$tags"
ok    "tags: busiest first (shared, count 2)"   "2  shared" "$(printf '%s\n' "$tags" | head -1)"
tl=$("$AIS" -f "$TT" --timeline)
ok    "timeline: dateless record shown first"   "(undated)" "$(printf '%s\n' "$tl" | head -1)"
ok    "timeline: hand-pasted record survived"   "pasted with no date" "$tl"
okeq  "timeline: all four records listed"        "4" "$(printf '%s\n' "$tl" | grep -c .)"
rm -rf "$TT"

# 17b. --update edits a record's keys by id (the handle): -KEY detaches, KEY
#      attaches; the record (id + value) survives, and a detach survives compact.
UP=$(mktemp -d "${TMPDIR:-/tmp}/ais_upd.XXXXXX") || exit 2
uid=$("$AIS" -f "$UP" -v "https://trip.example/venice" venice italy)
"$AIS" -f "$UP" --update "$uid" -- -venice                       # detach 'venice'
okempty "update: detached key recalls nothing"      "$("$AIS" -f "$UP" venice)"
ok      "update: record survives via another key"   "venice" "$("$AIS" -f "$UP" italy)"
case "$("$AIS" -f "$UP" --keys)" in
    *venice*) fail=$((fail + 1)); echo "  FAIL update: 'venice' still listed in --keys" ;;
    *)        pass=$((pass + 1)); echo "  ok   update: 'venice' gone from --keys" ;;
esac
"$AIS" -f "$UP" --update "$uid" venice                           # re-attach
ok      "update: re-attached key recalls again"     "venice" "$("$AIS" -f "$UP" venice)"
"$AIS" -f "$UP" --update "$uid" -- -italy >/dev/null             # detach + compact
"$AIS" -f "$UP" -y --compact >/dev/null
okempty "update: detach is durable through compact" "$("$AIS" -f "$UP" italy)"
ok      "update: other key survives compact"        "venice" "$("$AIS" -f "$UP" venice)"
"$AIS" -f "$UP" --update "$uid" tuscany >/dev/null               # ATTACH + compact
"$AIS" -f "$UP" -y --compact >/dev/null
ok      "update: attach is durable through compact" "venice" "$("$AIS" -f "$UP" tuscany)"
# and it reaches a peer: export reads the STORE, so a key posted only to idx/
# would never leave this device (the same defect, on the sync path).
UPD=$(mktemp -d "${TMPDIR:-/tmp}/ais_updpeer.XXXXXX") || exit 2
"$AIS" -f "$UP" --export > "$UPD/stream" 2>/dev/null
"$AIS" -f "$UPD" --import < "$UPD/stream" >/dev/null 2>&1
ok      "update: attached key reaches a peer"       "venice" "$("$AIS" -f "$UPD" tuscany)"
rm -rf "$UPD"
rm -rf "$UP"

# 17c. --set replaces ONE of a multi-link record's values in place, leaving the
#      other links, the id and the keys alone. Without it a wrong link could only
#      be removed by deleting the record, and a re-add of any matching value
#      resurrects it (put is last-write-wins), so delete-and-recreate restores
#      what it just removed.
SV=$(mktemp -d "${TMPDIR:-/tmp}/ais_set.XXXXXX") || exit 2
sid=$("$AIS" -f "$SV" -v "Some Article - https://example.com/a" -v "https://doi.org/10.5281/zenodo.111" articles papers)
"$AIS" -f "$SV" --set "$sid" -v "https://doi.org/10.5281/zenodo.111" -v "https://doi.org/10.5281/zenodo.110"
ok      "set: the new value is there"        "zenodo.110" "$("$AIS" -f "$SV" papers)"
okempty "set: the old value is gone"         "$("$AIS" -f "$SV" --find zenodo.111)"
ok      "set: the sibling link is untouched" "example.com/a" "$("$AIS" -f "$SV" papers)"
ok      "set: keys survive"                  "zenodo.110" "$("$AIS" -f "$SV" articles)"
okeq    "set: still one record"              "$sid" "$("$AIS" -f "$SV" papers | head -1 | cut -d'|' -f1)"
"$AIS" -f "$SV" --set "$sid" -v "no-such-value" -v "x" 2>/dev/null
ok      "set: a value that does not match leaves the store alone" "zenodo.110" "$("$AIS" -f "$SV" papers)"
# a multi-line NEW value must be refused, exactly as store_append refuses one on
# the put path: the rewrite would split the line and orphan the tail.
"$AIS" -f "$SV" --set "$sid" -v "https://doi.org/10.5281/zenodo.110" -v "$(printf 'a\nb')" 2>/dev/null
ok      "set: a multi-line value is refused"  "zenodo.110" "$("$AIS" -f "$SV" papers)"
okeq    "set: the store is still one line per link" "2" "$(grep -c . "$SV/store")"
rm -rf "$SV"

# 17d. An attached key is FOLDED before it reaches the store line ('|' and control
#      bytes -> '_'), the same as keys_attach_only does for a put. Keys share the
#      line's '|' delimiter, so a raw "a|b" would shift the value into the wrong
#      field; a raw newline would end the line and drop the value entirely.
FK=$(mktemp -d "${TMPDIR:-/tmp}/ais_fold.XXXXXX") || exit 2
fid=$("$AIS" -f "$FK" -v "http://rome" italy)
"$AIS" -f "$FK" --update "$fid" 'a|b' >/dev/null 2>&1
ok      "fold: the value survives a '|' in an attached key" "http://rome" "$("$AIS" -f "$FK" italy)"
ok      "fold: the key is stored folded"                    "a_b"         "$("$AIS" -f "$FK" --dump)"
"$AIS" -f "$FK" --update "$fid" "$(printf 'x\ny')" >/dev/null 2>&1
ok      "fold: the value survives a newline in an attached key" "http://rome" "$("$AIS" -f "$FK" italy)"
okeq    "fold: the store is still a single line"            "1" "$(grep -c . "$FK/store")"
rm -rf "$FK"

# 17e. The store is written BEFORE the index. A rewrite that cannot fit must leave
#      no posting behind: a key in idx/ that the store does not know about is
#      exactly what the next --compact drops, i.e. the loss the mirror prevents.
AT=$(mktemp -d "${TMPDIR:-/tmp}/ais_atom.XXXXXX") || exit 2
big=$(printf 'v%.0s' $(seq 1 65400))
longkey=$(printf 'z%.0s' $(seq 1 200))
aid=$("$AIS" -f "$AT" -v "$big" only)
"$AIS" -f "$AT" --update "$aid" alpha "$longkey" >/dev/null 2>&1   # 'alpha' fits, the pair does not
okeq    "atomic: the keys field is unchanged when the rewrite cannot fit" "only" "$(cut -d'|' -f3 "$AT/store")"
okempty "atomic: no phantom posting for the rejected keys"  "$(find "$AT/idx" -name 'alpha' -o -name "$longkey")"
rm -rf "$AT"

# 17f. --set refuses what would break value-identity: a deleted id (as --update
#      does), and a value another record already holds (put is idempotent by value
#      scan and tombstones are hash-stamped, so two records with one value make a
#      peer collapse them and a later delete of either take both).
GD=$(mktemp -d "${TMPDIR:-/tmp}/ais_guard.XXXXXX") || exit 2
d1=$("$AIS" -f "$GD" -v "http://one" g1)
d2=$("$AIS" -f "$GD" -v "http://two" g2)
"$AIS" -f "$GD" --set "$d2" -v "http://two" -v "http://one" 2>/dev/null
ok      "set: refuses a value another record holds" "http://two" "$("$AIS" -f "$GD" g2)"
"$AIS" -f "$GD" -y --del "$d1" >/dev/null 2>&1
"$AIS" -f "$GD" --set "$d1" -v "http://one" -v "http://three" 2>/dev/null
okempty "set: refuses a deleted id"                 "$("$AIS" -f "$GD" --find http://three)"
rm -rf "$GD"

# 17g. Encode-equivalent spellings are ONE key. idx/ holds a single entry for
#      "Doc"/"doc"/"DOC", so counting them as separate tokens would grow the keys
#      field on every re-put until the line no longer fits.
CS=$(mktemp -d "${TMPDIR:-/tmp}/ais_case.XXXXXX") || exit 2
"$AIS" -f "$CS" -v "CASEVAL" Doc >/dev/null
for k in doc DOC dOc DoC; do "$AIS" -f "$CS" -v "CASEVAL" $k >/dev/null; done
okeq    "case: one token kept, first spelling wins" "Doc" "$(cut -d'|' -f3 "$CS/store")"
ok      "case: recall still works by any spelling"  "CASEVAL" "$("$AIS" -f "$CS" DOC)"
rm -rf "$CS"

# 17h. A detach must stick through compaction whatever the spelling. ktomb stores
#      and compares the key_encode() form -- the identity the postings use -- so
#      detaching "doc" removes a stored "Doc". A raw strcmp did not match, and
#      compaction then RESURRECTED the key it had just removed. Encoding also folds
#      the '|' that would otherwise split the ktomb line's own fields.
DT=$(mktemp -d "${TMPDIR:-/tmp}/ais_det.XXXXXX") || exit 2
t1=$("$AIS" -f "$DT" -v "DETVAL" Doc)
"$AIS" -f "$DT" --update "$t1" -- -doc >/dev/null 2>&1
okempty "detach: a differently-spelled detach hides the key"  "$("$AIS" -f "$DT" doc)"
"$AIS" -f "$DT" -y --compact >/dev/null
okempty "detach: and it stays hidden through compaction"      "$("$AIS" -f "$DT" doc)"
t2=$("$AIS" -f "$DT" -v "PIPEVAL" 'a|b')
"$AIS" -f "$DT" --update "$t2" -- '-a|b' >/dev/null 2>&1
"$AIS" -f "$DT" -y --compact >/dev/null
okempty "detach: a '|' key detaches and stays detached"       "$("$AIS" -f "$DT" 'a|b')"
# the records themselves survive: only the keys were detached (Doc was that
# record's ONLY key, so it is correctly unreachable by key, not deleted).
ok      "detach: the record itself survives"                  "DETVAL"  "$("$AIS" -f "$DT" --find DETVAL)"
ok      "detach: the '|' record survives too"                 "PIPEVAL" "$("$AIS" -f "$DT" --find PIPEVAL)"
rm -rf "$DT"

# 17i. --del-key SHOWS what it will destroy before asking, and says plainly that it
#      deletes the RECORDS, not the tag -- the command's name reads like "untag",
#      which is the one thing it does not do. Answering no must change nothing.
DK=$(mktemp -d "${TMPDIR:-/tmp}/ais_delkey.XXXXXX") || exit 2
"$AIS" -f "$DK" -v "http://keep-a" proj alpha >/dev/null
"$AIS" -f "$DK" -v "http://keep-b" proj beta  >/dev/null
"$AIS" -f "$DK" -v "http://other"  unrelated  >/dev/null
# the answer comes from AIS_TTY, the same seam --import-interactively uses: the
# prompt must NOT be answerable by redirected data on stdin.
DKT="$DK/answer"; printf 'n\n' > "$DKT"
prev=$(AIS_TTY="$DKT" "$AIS" -f "$DK" --del-key proj 2>&1)
ok      "del-key: names the key it would empty"    "Filed under 'proj'" "$prev"
ok      "del-key: says records, not the tag"       "deletes RECORDS, not the tag" "$prev"
ok      "del-key: lists what would go"             "http://keep-a" "$prev"
# the remedy must be the command that EXISTS for this, not a hand-rolled
# --update detach the user has to compose per record.
ok      "del-key: offers the non-destructive path" "ais \-\-untag proj" "$prev"
ok      "del-key: warns about the other keys"      "every other key" "$prev"
ok      "del-key: the prompt repeats the count"    "these 2 record" "$prev"
okeq    "del-key: declining deletes nothing"       "2" "$("$AIS" -f "$DK" proj | grep -c .)"
# AIS_TTY is a TEST seam. If it is left set in an environment, a destructive
# command must not be confirmed silently by a file -- it has to say who answered.
ok      "del-key: says the answer came from AIS_TTY" "reading the answer from AIS_TTY" "$prev"
AIS_TTY="$DKT" "$AIS" -f "$DK" --del-key proj >/dev/null 2>&1
okeq    "del-key: declining exits non-zero"        "1" "$?"
# a redirected data file must NOT be able to answer the prompt
printf 'yes I really do\n' > "$DK/data"
AIS_TTY=/dev/null "$AIS" -f "$DK" --del-key proj < "$DK/data" >/dev/null 2>&1
okeq    "del-key: an empty terminal answer exits 2" "2" "$?"
# The seam is that the answer comes from the TERMINAL, never stdin. /dev/null only
# proves the EOF branch, so pit the two against each other: with the terminal
# saying no and stdin saying yes, the delete must NOT happen (and vice versa).
printf 'y\n' > "$DK/stdin_yes"
printf 'n\n' > "$DK/tty_no"
AIS_TTY="$DK/tty_no" "$AIS" -f "$DK" --del-key proj < "$DK/stdin_yes" >/dev/null 2>&1
okeq    "del-key: a 'y' on stdin cannot confirm"   "2" "$("$AIS" -f "$DK" proj | grep -c .)"
printf 'n\n' > "$DK/stdin_no"
printf 'y\n' > "$DK/tty_yes"
AIS_TTY="$DK/tty_yes" "$AIS" -f "$DK" --del-key proj < "$DK/stdin_no" >/dev/null 2>&1
okempty "del-key: the TERMINAL answer is the one that counts" "$("$AIS" -f "$DK" proj)"
"$AIS" -f "$DK" -v "http://keep-a" proj alpha >/dev/null   # restore the fixture
"$AIS" -f "$DK" -v "http://keep-b" proj beta  >/dev/null
okeq    "del-key: and nothing was deleted"         "2" "$("$AIS" -f "$DK" proj | grep -c .)"
# only a real y/yes confirms: "yolo" must not
printf 'yolo\n' > "$DKT"
AIS_TTY="$DKT" "$AIS" -f "$DK" --del-key proj >/dev/null 2>&1
okeq    "del-key: 'yolo' does not confirm"         "2" "$("$AIS" -f "$DK" proj | grep -c .)"
ok      "del-key: the unrelated record is untouched" "http://other" "$("$AIS" -f "$DK" unrelated)"
# an empty key says so and asks nothing
none=$(printf 'n\n' | "$AIS" -f "$DK" --del-key nosuchkey 2>&1)
ok      "del-key: an unused key asks nothing"      "nothing is filed" "$none"
# and -y still skips the preview entirely, for scripts
"$AIS" -f "$DK" -y --del-key proj >/dev/null 2>&1
okempty "del-key: -y deletes without prompting"    "$("$AIS" -f "$DK" proj)"
ok      "del-key: -y left other records alone"     "http://other" "$("$AIS" -f "$DK" unrelated)"
rm -rf "$DK"

# 17i-bis. The manifest's SHAPE, not just its wording. 17i asserts the text with
#      a 2-record single-link fixture, which cannot see any of: the 10-record cap,
#      per-value truncation, the multi-link summary, or -- the one the code calls
#      the most dangerous state -- the manifest going to stdout instead of stderr.
PV=$(mktemp -d "${TMPDIR:-/tmp}/ais_preview.XXXXXX") || exit 2
printf 'n\n' > "$PV/ans"
LONG="http://example.com/$(printf 'x%.0s' 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 \
                                          1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 \
                                          1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 \
                                          1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 \
                                          1 2 3 4 5 6 7 8 9 0)"
"$AIS" -f "$PV" -v "$LONG" pk >/dev/null
"$AIS" -f "$PV" -v http://link-a pk >/dev/null
"$AIS" -f "$PV" --add 2 -v http://link-b >/dev/null    # id 2 holds TWO links
i=1; while [ $i -le 12 ]; do "$AIS" -f "$PV" -v "http://p$i" pk >/dev/null; i=$((i+1)); done
perr=$(AIS_TTY="$PV/ans" "$AIS" -f "$PV" --del-under pk 2>&1 >/dev/null)
pout=$(AIS_TTY="$PV/ans" "$AIS" -f "$PV" --del-under pk 2>/dev/null)
# `ais --del-under k > file` must not show the user an EMPTY kill-list
okempty "del-under: the manifest never goes to stdout" "$pout"
ok      "del-under: the manifest goes to stderr"    "http://link-a" "$perr"
ok      "del-under: a multi-link record says how many links go" \
        "(+1 more link on this record)" "$perr"
ok      "del-under: the manifest caps at 10"        "and 4 more" "$perr"
okeq    "del-under: exactly 10 record lines"        "10" \
        "$(printf '%s\n' "$perr" | grep -c '^  [0-9]*|')"
ok      "del-under: a long value is truncated"      "xxx\.\.\.$" "$perr"
rm -rf "$PV"

# 17i-ter. The prompt's own numbers, and its singular form. 17i/17j assert the
#      RESULT line but never that the prompt agreed with it.
PS=$(mktemp -d "${TMPDIR:-/tmp}/ais_prompt.XXXXXX") || exit 2
printf 'n\n' > "$PS/ans"
"$AIS" -f "$PS" -v s1 sk >/dev/null
one=$(AIS_TTY="$PS/ans" "$AIS" -f "$PS" --del-under sk 2>&1)
ok      "del-under: singular prompt"     "this 1 record filed under" "$one"
"$AIS" -f "$PS" -v s2 sk >/dev/null
"$AIS" -f "$PS" -v s3 sk >/dev/null
two=$(AIS_TTY="$PS/ans" "$AIS" -f "$PS" --del-under sk 2>&1)
ok      "del-under: plural prompt states the count" "these 3 records filed under" "$two"
utp=$(AIS_TTY="$PS/ans" "$AIS" -f "$PS" --untag sk 2>&1)
ok      "untag: the prompt states the count"        "from 3 records" "$utp"
rm -rf "$PS"

# 17i-quater. confirm() accepts exactly y/Y/yes/YES and nothing else. 17i only
#      ever feeds it lowercase 'y' and 'yolo', so the other forms were unpinned.
CF=$(mktemp -d "${TMPDIR:-/tmp}/ais_confirm.XXXXXX") || exit 2
for ans in Y yes YES; do
    "$AIS" -f "$CF" -v "cv$ans" cfk >/dev/null
    printf '%s\n' "$ans" > "$CF/a"
    AIS_TTY="$CF/a" "$AIS" -f "$CF" --del-under cfk >/dev/null 2>&1
    okempty "confirm: '$ans' confirms" "$("$AIS" -f "$CF" cfk)"
done
"$AIS" -f "$CF" -v cvspace cfk >/dev/null
printf 'y \n' > "$CF/a"                      # a trailing space is not consent
AIS_TTY="$CF/a" "$AIS" -f "$CF" --del-under cfk >/dev/null 2>&1
ok      "confirm: 'y ' does NOT confirm"  "cvspace" "$("$AIS" -f "$CF" cfk)"
# the two exit-2 paths must stay distinguishable: no terminal vs no answer
noterm=$(AIS_TTY=/nonexistent/path "$AIS" -f "$CF" --del-under cfk 2>&1); nrc=$?
ok      "confirm: an unopenable terminal says so" "no terminal to confirm on" "$noterm"
okeq    "confirm: and exits 2"            "2" "$nrc"
rm -rf "$CF"

# 17i-quinquies. --del-under shreds encrypted blobs before tombstoning, the same
#      promise --del makes: the ciphertext must not outlive the record. The blob
#      reference is written by hand so this pins the WIRING (which is what a
#      regression drops) without needing the crypto module to be built.
SH=$(mktemp -d "${TMPDIR:-/tmp}/ais_shred.XXXXXX") || exit 2
mkdir -p "$SH/blobs"
printf 'ciphertext-bytes\n' > "$SH/blobs/s1.aisc"
printf 'ciphertext-bytes\n' > "$SH/blobs/s2.aisc"
"$AIS" -f "$SH" -v 'aisc:@blobs/s1.aisc' shk >/dev/null
"$AIS" -f "$SH" -v 'aisc:@blobs/s2.aisc' other >/dev/null
"$AIS" -f "$SH" -y --del-under shk >/dev/null 2>&1
okeq    "del-under: the deleted record's blob was shredded" "0" \
        "$([ -e "$SH/blobs/s1.aisc" ] && echo 1 || echo 0)"
okeq    "del-under: an unrelated record's blob is untouched" "1" \
        "$([ -e "$SH/blobs/s2.aisc" ] && echo 1 || echo 0)"
"$AIS" -f "$SH" -y --del 2 >/dev/null 2>&1
okeq    "del: shreds the blob too (the same promise)"  "0" \
        "$([ -e "$SH/blobs/s2.aisc" ] && echo 1 || echo 0)"
rm -rf "$SH"

# 17j. --untag removes the TAG and keeps the records: the non-destructive
#      counterpart, and the thing most people mean by "delete a key". A record
#      filed elsewhere stays reachable there, and the removal survives compaction
#      (it is a ktomb entry, exactly like a single --update detach).
UT=$(mktemp -d "${TMPDIR:-/tmp}/ais_untag.XXXXXX") || exit 2
UTT="$UT/answer"; printf 'y\n' > "$UTT"
"$AIS" -f "$UT" -v "http://u-one" proj deploy >/dev/null
"$AIS" -f "$UT" -v "http://u-two" proj s3     >/dev/null
"$AIS" -f "$UT" -v "http://u-solo" proj       >/dev/null
out=$(AIS_TTY="$UTT" "$AIS" -f "$UT" --untag proj 2>&1)
ok      "untag: says the records are kept"    "records are kept" "$out"
ok      "untag: reports how many"             "untagged 3$"      "$out"
okempty "untag: the tag is gone"              "$("$AIS" -f "$UT" proj)"
ok      "untag: a record keeps its other tags" "http://u-one" "$("$AIS" -f "$UT" deploy)"
ok      "untag: and the other one too"         "http://u-two" "$("$AIS" -f "$UT" s3)"
ok      "untag: a record with no other tag survives" "http://u-solo" "$("$AIS" -f "$UT" --find u-solo)"
"$AIS" -f "$UT" -y --compact >/dev/null
okempty "untag: still gone after compaction"  "$("$AIS" -f "$UT" proj)"
ok      "untag: records still there after compaction" "http://u-one" "$("$AIS" -f "$UT" deploy)"
# re-tagging brings it back: untag is reversible, unlike --del-under
"$AIS" -f "$UT" -v "http://u-one" proj >/dev/null
ok      "untag: re-tagging restores it"       "http://u-one" "$("$AIS" -f "$UT" proj)"
printf 'n\n' > "$UTT"
AIS_TTY="$UTT" "$AIS" -f "$UT" --untag deploy >/dev/null 2>&1
okeq    "untag: declining exits non-zero"     "1" "$?"
ok      "untag: declining changes nothing"    "http://u-one" "$("$AIS" -f "$UT" deploy)"
none=$(AIS_TTY="$UTT" "$AIS" -f "$UT" --untag nosuch 2>&1)
ok      "untag: an unused key asks nothing"   "nothing is filed" "$none"
rm -rf "$UT"

# 17j-bis. A posting still lists ids whose records are TOMBSTONED (removal is
#      physical only at compaction). Treating those as a failure aborted the untag
#      part-way and then failed identically on every retry, so any index that had
#      ever seen a delete could not untag at all. Also crosses the 64-id batch.
UD=$(mktemp -d "${TMPDIR:-/tmp}/ais_untagdel.XXXXXX") || exit 2
"$AIS" -f "$UD" -v "http://d-one" dproj keepme >/dev/null
"$AIS" -f "$UD" -v "http://d-two" dproj >/dev/null
"$AIS" -f "$UD" -v "http://d-three" dproj >/dev/null
"$AIS" -f "$UD" -y --del 2 >/dev/null
tmo 20 "$AIS" -f "$UD" -y --untag dproj >/dev/null 2>&1
okeq    "untag: a deleted record does not block the untag" "0" "$?"
okempty "untag: the tag is fully gone despite the tombstone" "$("$AIS" -f "$UD" dproj)"
ok      "untag: the live records survived"    "http://d-one"   "$("$AIS" -f "$UD" keepme)"
ok      "untag: the last record survived too" "http://d-three" "$("$AIS" -f "$UD" --find d-three)"
# More than one batch of 64, with deletions ON the boundary. 3 records already
# exist, so bulkkey's posting holds ids 4..83 and the first batch ends at id 67 --
# ids 64/65/66 sit at posting positions 61/62/63, comfortably INSIDE batch one and
# straddling nothing. 67 and 68 are the last of batch one and the first of batch two.
i=1; while [ $i -le 80 ]; do "$AIS" -f "$UD" -v "http://bulk$i" bulkkey >/dev/null; i=$((i+1)); done
for d in 67 68; do "$AIS" -f "$UD" -y --del "$d" >/dev/null 2>&1; done
bulk=$(tmo 30 "$AIS" -f "$UD" -y --untag bulkkey 2>&1); brc=$?
okeq    "untag: works across the batch boundary" "0" "$brc"
# the count is asserted too: discarding it hid any miscount in the second batch
ok      "untag: counts every live record across both batches" "untagged 78$" "$bulk"
okempty "untag: nothing left under the bulk key" "$("$AIS" -f "$UD" bulkkey)"
rm -rf "$UD"

# 17j-ter. --untag can leave a record with NO keys, and that record must survive
#      the documented backup pipeline. `--dump | --import` used to drop exactly
#      those lines, so restoring a backup silently lost them.
KL=$(mktemp -d "${TMPDIR:-/tmp}/ais_keyless.XXXXXX") || exit 2
KR=$(mktemp -d "${TMPDIR:-/tmp}/ais_keyless2.XXXXXX") || exit 2
"$AIS" -f "$KL" -v "http://solo" onlykey >/dev/null
"$AIS" -f "$KL" -v "http://kept" ka kb   >/dev/null
"$AIS" -f "$KL" -y --untag onlykey >/dev/null 2>&1
ok      "keyless: untag leaves the record in the dump" "http://solo" "$("$AIS" -f "$KL" --dump)"
"$AIS" -f "$KL" --dump | "$AIS" -f "$KR" --import >/dev/null 2>&1
ok      "keyless: it survives dump|import"    "http://solo" "$("$AIS" -f "$KR" --dump)"
ok      "keyless: the keyed record too"       "http://kept" "$("$AIS" -f "$KR" ka)"
okeq    "keyless: both records restored"      "2" "$("$AIS" -f "$KR" --dump | grep -c .)"
# the id prefix is recognised only when field 1 is a BARE INTEGER: without that
# check a hand-written "key|value|more" line loses its key to the id-stripper
mk=$(printf 'mykey|http://a|b\n' | "$AIS" -f "$KR" --import 2>&1)
ok      "keyless: a hand-written first field is a KEY, not an id" "mykey" \
        "$("$AIS" -f "$KR" --keys)"
ok      "keyless: and its value kept both bars"  "http://a|b" "$("$AIS" -f "$KR" mykey)"
# a HAND-WRITTEN keyless line has no id to vouch for it and is still a typo
typo=$(printf '|http://typo\n' | "$AIS" -f "$KR" --import 2>&1)
ok      "keyless: a hand-written empty key is still refused" "empty keys, skipped" "$typo"
# A future verb must be REFUSED, not turned into a record. Checking one letter let
# a two-character verb through, so "D2|<ts>|<hash>" landed as a record keyed "D2" --
# the exact corruption the guard exists to stop, and the first thing a v2 verb
# would have hit. The date test is what keeps a real tag safe.
VB=$(mktemp -d "${TMPDIR:-/tmp}/ais_verbs.XXXXXX") || exit 2
vb=$(printf 'D2|2026-01-02T00:00:00Z|deadbeef\nE|2026-01-02T00:00:00Z|cafebabe\n' \
     | "$AIS" -f "$VB" --import 2>&1)
ok      "verbs: a two-character future verb is refused" "unknown 'D2|'" "$vb"
ok      "verbs: a one-character future verb is refused" "unknown 'E|'"  "$vb"
okeq    "verbs: neither became a record"        "0" "$("$AIS" -f "$VB" --dump | grep -c .)"
# but a tag that merely LOOKS like a verb still imports: no date in field 2
printf 'TODO|buy milk\n' | "$AIS" -f "$VB" --import >/dev/null 2>&1
ok      "verbs: a real tag is not mistaken for one" "buy milk" "$("$AIS" -f "$VB" TODO)"
rm -rf "$VB"
okeq    "keyless: and nothing was added"      "3" "$("$AIS" -f "$KR" --dump | grep -c .)"
rm -rf "$KL" "$KR"

# 17j-quater. A key with whitespace HUNG forever and corrupted the store: "-a b"
#      tokenised into a detach of 'a' and an ATTACH of 'b', while the posting being
#      polled was 'a_b' and so never shrank. The key is folded the way the posting
#      names it, and the walk advances a cursor so no id can spin the loop.
WS=$(mktemp -d "${TMPDIR:-/tmp}/ais_wskey.XXXXXX") || exit 2
"$AIS" -f "$WS" -v "http://w1" a_b >/dev/null
"$AIS" -f "$WS" -v "http://w2" a_b >/dev/null
wout=$(tmo 20 "$AIS" -f "$WS" -y --untag "a b" 2>&1); wrc=$?
okeq    "untag: a whitespace key terminates"  "0" "$wrc"
ok      "untag: it folds to the posting's name" "untagged 2$" "$wout"
okempty "untag: the folded key is gone"       "$("$AIS" -f "$WS" a_b)"
okeq    "untag: no bogus key was attached"    "0" "$("$AIS" -f "$WS" --keys | grep -c .)"
ok      "untag: the records are intact"       "http://w1" "$("$AIS" -f "$WS" --find w1)"
rm -rf "$WS"

# 17j-quinquies. Untagging a TOMBSTONED record must still RECORD the detach.
#      Pruning the posting alone left the key in the authoritative keys field with
#      nothing masking it, so resurrecting the record (a re-put of the same value,
#      or a peer's newer A|) brought the tag back at the next compaction.
RS=$(mktemp -d "${TMPDIR:-/tmp}/ais_resurrect.XXXXXX") || exit 2
"$AIS" -f "$RS" -v rv1 tg c1 >/dev/null
"$AIS" -f "$RS" -v rv2 tg c2 >/dev/null
"$AIS" -f "$RS" -y --del 2 >/dev/null
tmo 20 "$AIS" -f "$RS" -y --untag tg >/dev/null 2>&1
"$AIS" -f "$RS" -v rv2 zz >/dev/null                 # resurrect the deleted record
"$AIS" -f "$RS" -y --compact >/dev/null
okeq    "untag: the tag does not come back on resurrect" "0" "$("$AIS" -f "$RS" --keys | grep -c '^tg$')"
okempty "untag: and nothing answers under it"          "$("$AIS" -f "$RS" tg)"
ok      "untag: the resurrected record is still there" "rv2" "$("$AIS" -f "$RS" zz)"
rm -rf "$RS"

# 17j-sexies. A posting can name an id with NO store line (a hand-edited index, or
#      a store restored without its idx/). Failing there aborted the untag PART WAY
#      and then failed identically on every retry, wedging the key forever.
GH=$(mktemp -d "${TMPDIR:-/tmp}/ais_ghost.XXXXXX") || exit 2
"$AIS" -f "$GH" -v g1 hk >/dev/null
"$AIS" -f "$GH" -v g2 hk >/dev/null
"$AIS" -f "$GH" -v g3 hk >/dev/null
gpost=$(ls "$GH"/idx/*/hk)
echo 999 >> "$gpost"                                  # an id that was never stored
tmo 20 "$AIS" -f "$GH" -y --untag hk >/dev/null 2>&1
okeq    "untag: a ghost posting entry does not wedge the key" "0" "$?"
okempty "untag: the key is fully gone"                "$("$AIS" -f "$GH" hk)"
okeq    "untag: every real record was untagged"       "3" "$("$AIS" -f "$GH" --dump | grep -c .)"
rm -rf "$GH"

# 17j-sexies-bis. A posting can also hold a NON-POSITIVE id (a hand edit, or a
#      truncated write). The collection cursor starts below every id so those are
#      pruned; starting it at 0 skipped them and left the key alive forever.
NP=$(mktemp -d "${TMPDIR:-/tmp}/ais_nonpos.XXXXXX") || exit 2
"$AIS" -f "$NP" -v n1 zk >/dev/null
"$AIS" -f "$NP" -v n2 zk >/dev/null
npost=$(ls "$NP"/idx/*/zk)          # dash does not glob a redirection TARGET
printf '0\n-1\n' >> "$npost"
tmo 20 "$AIS" -f "$NP" -y --untag zk >/dev/null 2>&1
okeq    "untag: a non-positive posting id does not leave the key alive" \
        "0" "$("$AIS" -f "$NP" --keys | grep -c '^zk$')"
okempty "untag: nothing answers under it"     "$("$AIS" -f "$NP" zk)"
rm -rf "$NP"

# 17j-sexies-ter. A posting duplicated by a hand edit must be counted ONCE: the
#      prompt counts live records, so a double count made the two disagree.
DP=$(mktemp -d "${TMPDIR:-/tmp}/ais_duppost.XXXXXX") || exit 2
"$AIS" -f "$DP" -v d1 dk >/dev/null
"$AIS" -f "$DP" -v d2 dk >/dev/null
dpost=$(ls "$DP"/idx/*/dk)
printf '1\n' >> "$dpost"
ok      "untag: a duplicated posting entry is counted once" "untagged 2$" \
        "$(tmo 20 "$AIS" -f "$DP" -y --untag dk 2>&1)"
rm -rf "$DP"

# 17j-septies. --del-under RE-STAMPS an already-deleted record (so "delete
#      everything under this key" holds as of now, and a peer add dated between the
#      two deletes stays suppressed) but does NOT count it -- counting it made the
#      prompt say one number and the result line another.
RC=$(mktemp -d "${TMPDIR:-/tmp}/ais_restamp.XXXXXX") || exit 2
"$AIS" -f "$RC" -v c1 ck >/dev/null
"$AIS" -f "$RC" -v c2 ck >/dev/null
"$AIS" -f "$RC" -y --del 1 >/dev/null
ok      "del-under: counts only the LIVE records"     "deleted 1$" \
        "$("$AIS" -f "$RC" -y --del-under ck 2>&1)"
# The re-stamp REPLACES rather than accumulates: "everything under this key is
# deleted as of now" is one fact, not a running tally, and every duplicate costs
# each peer a full store scan on every future import.
okeq    "del-under: re-stamping does not grow the tomb" "1" "$(grep -c '^1|' "$RC"/tomb)"
okempty "del-under: and the record stays deleted"       "$("$AIS" -f "$RC" ck)"
rm -rf "$RC"

# 17j-octies. --del on an already-deleted id must not offer to delete it again:
#      ais_record reads the store, which still holds tombstoned lines.
DD=$(mktemp -d "${TMPDIR:-/tmp}/ais_deldead.XXXXXX") || exit 2
"$AIS" -f "$DD" -v dv1 dk >/dev/null
"$AIS" -f "$DD" -y --del 1 >/dev/null
printf 'y\n' > "$DD/ans"
dead=$(AIS_TTY="$DD/ans" "$AIS" -f "$DD" --del 1 2>&1); drc=$?
ok      "del: an already-deleted id says so"          "no live record 1" "$dead"
okeq    "del: and exits non-zero without asking"      "1" "$drc"
okeq    "del: no second tombstone was appended"       "1" "$(grep -c '^1|' "$DD"/tomb)"
rm -rf "$DD"

# 17j-nonies. The small refusals and notices, each of which a regression can drop
#      silently: a missing KEY must not be a no-op success, declining --compact must
#      not report success, and an index with sync state must be warned that --set
#      does not propagate.
MS=$(mktemp -d "${TMPDIR:-/tmp}/ais_misc.XXXXXX") || exit 2
"$AIS" -f "$MS" -v m1 mk >/dev/null
"$AIS" -f "$MS" --untag >/dev/null 2>&1
okeq    "untag: a missing KEY is an error, not a no-op" "1" "$?"
"$AIS" -f "$MS" --del-under >/dev/null 2>&1
okeq    "del-under: a missing KEY is an error too"      "1" "$?"
printf 'n\n' > "$MS/ans"
AIS_TTY="$MS/ans" "$AIS" -f "$MS" --compact >/dev/null 2>&1
okeq    "compact: declining exits non-zero"             "1" "$?"
ok      "compact: and the record is still there"        "m1" "$("$AIS" -f "$MS" mk)"
# --set is LOCAL ONLY: it has no merge verb, so a synced index must be told
printf 'peer-state\n' > "$MS/syncid"
ok      "set: warns when the index syncs"  "does not propagate" \
        "$("$AIS" -f "$MS" --set 1 -v m1 -v m2 2>&1)"
rm -rf "$MS"

# 17m. A value the index once DELETED must be saveable again, and the re-save has
#      to survive the next sync. The record kept its ORIGINAL creation time when it
#      came back, so it exported as an A| older than the peer's tombstone: the peer
#      kept the delete and sent its D| back, killing it here too. Re-saving anything
#      ever deleted was impossible, permanently, on every device.
#      Dated lines, not sleeps, so the test is deterministic.
# The digest is salted with the record's CREATION ts, so it has to be taken from a
# record created at the same instant as the one it will be matched against -- here,
# the dated 2020 line. Deleting a copy is how we learn that digest.
RH=$(mktemp -d "${TMPDIR:-/tmp}/ais_readdh.XXXXXX") || exit 2
printf 'A|2020-01-01T00:00:00Z|reading|http://x/readd\n' | "$AIS" -f "$RH" --import >/dev/null 2>&1
"$AIS" -f "$RH" -y --del 1 >/dev/null 2>&1
hash=$(cut -d'|' -f3 < "$RH/tomb")
rm -rf "$RH"
RA=$(mktemp -d "${TMPDIR:-/tmp}/ais_readd2.XXXXXX") || exit 2
# a peer's record from long ago, then a delete dated later: the record goes
printf 'A|2020-01-01T00:00:00Z|reading|http://x/readd\n' | "$AIS" -f "$RA" --import >/dev/null 2>&1
printf 'D|2020-06-01T00:00:00Z|%s\n' "$hash" | "$AIS" -f "$RA" --import >/dev/null 2>&1
okempty "readd: the dated delete removed it"        "$("$AIS" -f "$RA" reading)"
# the user changes their mind and saves it again (stamped now, well after 2020)
"$AIS" -f "$RA" -v "http://x/readd" reading >/dev/null
ok      "readd: saving it again brings it back"     "http://x/readd" "$("$AIS" -f "$RA" reading)"
# THE POINT: the exported add must be NEWER than the delete, or the next sync kills it
newts=$("$AIS" -f "$RA" --export | grep 'http://x/readd' | cut -d'|' -f2)
if [ -n "$newts" ] && [ "$newts" \> "2020-06-01T00:00:00Z" ]; then
    pass=$((pass + 1)); echo "  ok   readd: it exports NEWER than the delete it survived"
else
    fail=$((fail + 1)); echo "  FAIL readd: exported ts '$newts' does not outrank the delete"
fi
# and a peer replaying its own stale delete must no longer win
printf 'D|2020-06-01T00:00:00Z|%s\n' "$hash" | "$AIS" -f "$RA" --import >/dev/null 2>&1
ok      "readd: a stale delete no longer kills it"  "http://x/readd" "$("$AIS" -f "$RA" reading)"
rm -rf "$RA"

# 17n. A tag whose every record is deleted must stop being offered. The posting
#      keeps a deleted record's id until compaction, so the tag list said
#      "notes 120" while querying `notes` returned nothing -- and in the GUIs, where
#      the tag list IS the way to browse, tapping it gave 0 results. On a phone
#      there is no CLI, so compaction never runs and the dead tags never clear.
PT=$(mktemp -d "${TMPDIR:-/tmp}/ais_phantom.XXXXXX") || exit 2
"$AIS" -f "$PT" -v p1 parents >/dev/null
"$AIS" -f "$PT" -v p2 parents >/dev/null
"$AIS" -f "$PT" -v p3 keep    >/dev/null
ok      "phantom: the tag counts 2 while both live" "2  parents" "$("$AIS" -f "$PT" --tags)"
"$AIS" -f "$PT" -y --del 1 >/dev/null
ok      "phantom: the count drops with one deleted"  "1  parents" "$("$AIS" -f "$PT" --tags)"
"$AIS" -f "$PT" -y --del 2 >/dev/null
okempty "phantom: querying the empty tag returns nothing" "$("$AIS" -f "$PT" parents)"
okeq    "phantom: --tags no longer offers it"        "0" \
        "$("$AIS" -f "$PT" --tags | grep -c 'parents')"
okeq    "phantom: --keys no longer lists it"         "0" \
        "$("$AIS" -f "$PT" --keys | grep -c '^parents$')"
ok      "phantom: the live tag is untouched"         "keep" "$("$AIS" -f "$PT" --keys)"
# and it must come BACK when something live is filed under it again
"$AIS" -f "$PT" -v p9 parents >/dev/null
ok      "phantom: it returns when a live record uses it" "parents" "$("$AIS" -f "$PT" --keys)"
ok      "phantom: and counts 1, not 3"               "1  parents" "$("$AIS" -f "$PT" --tags)"
rm -rf "$PT"

# 17o. A long-running process must see records written by ANY other process. The
#      id counter is cached at open, and the timeline's ceiling came from it, so a
#      server or a phone app that stayed up never showed anything added elsewhere --
#      including records arriving by sync. get/tags saw them; only "Recent" lied.
TLF=$(mktemp -d "${TMPDIR:-/tmp}/ais_tlfresh.XXXXXX") || exit 2
"$AIS" -f "$TLF" -v "https://seed.example" seed >/dev/null
tlport=$(( 19700 + ($$ % 200) ))
AIS_NO_OPEN=1 "$AIS" -f "$TLF" --serve "$tlport" >/dev/null 2>&1 &
tlsrv=$!
i=0; while [ $i -lt 50 ]; do curl -s -o /dev/null "http://127.0.0.1:$tlport/" && break; i=$((i+1)); sleep 0.1; done
if curl -s -o /dev/null "http://127.0.0.1:$tlport/" 2>/dev/null; then
    curl -s "http://127.0.0.1:$tlport/api/timeline?count=20" >/dev/null   # warm the cached counter
    "$AIS" -f "$TLF" -v "https://added.later" science >/dev/null          # a DIFFERENT process
    tl=$(curl -s "http://127.0.0.1:$tlport/api/timeline?count=20")
    ok   "timeline: a running server sees another writer's record" "added.later" "$tl"
    ok   "timeline: and still shows the original"                  "seed.example" "$tl"
else
    echo "  note could not bind $tlport -- skipping the live-server timeline check"
fi
kill "$tlsrv" 2>/dev/null
rm -rf "$TLF"

# 17p. A compaction KILLED mid-flight must not leave keyed lookups quietly wrong.
#      The staging in compact_locked rolls idx.bak back on a graceful error, but a
#      SIGKILL/OOM/power cut never reaches that path: it leaves a half-built idx/
#      live and the good tree orphaned in idx.bak. get() reads idx/ with NO store
#      fallback, so every keyed lookup then returned a SUBSET -- exit 0, no warning,
#      until somebody happened to compact again. Measured before the fix: a key that
#      should recall 1492 records returned 180.
#      The crashed state is built by hand so the test is deterministic and fast.
CR=$(mktemp -d "${TMPDIR:-/tmp}/ais_crashcompact.XXXXXX") || exit 2
i=1; while [ $i -le 40 ]; do "$AIS" -f "$CR" -v "rec-$i" bulk >/dev/null; i=$((i+1)); done
want=$("$AIS" -f "$CR" bulk | grep -c .)
okeq    "crash-compact: baseline recall"          "40" "$want"
# exactly what a killed run leaves behind: good tree staged, live tree half-built
mv "$CR/idx" "$CR/idx.bak"
mkdir -p "$CR/idx"
# check the crashed shape on the FILESYSTEM, not through ais: opening the index is
# what triggers recovery, so any query would heal it before it could observe it
okeq    "crash-compact: the live tree is empty, the good one staged" "0" \
        "$(find "$CR/idx" -type f | wc -l | tr -d ' ')"
okeq    "crash-compact: the staged tree holds the postings" "1" \
        "$([ "$(find "$CR/idx.bak" -type f | wc -l | tr -d ' ')" -gt 0 ] && echo 1 || echo 0)"
# the first open heals it: without recovery this recalls 0 of 40
okeq    "crash-compact: the next open restores the tree"   "40" "$("$AIS" -f "$CR" bulk | grep -c .)"
okeq    "crash-compact: and the staged copy is cleared"    "0" \
        "$([ -d "$CR/idx.bak" ] && echo 1 || echo 0)"
ok      "crash-compact: records are intact"        "rec-7" "$("$AIS" -f "$CR" --find rec-7)"
rm -rf "$CR"

# 17q. --compact --forget-deleted makes a deletion final HERE. A tombstone keeps a
#      hash of the deleted value so the delete can reach other devices -- but that
#      hash is FNV-1a over a often-guessable value, it is exported to every peer,
#      and it is kept for the life of the index. So "I deleted it" left a permanent,
#      testable trace of what was deleted. This gives the user a way to say no.
FD=$(mktemp -d "${TMPDIR:-/tmp}/ais_forget.XXXXXX") || exit 2
"$AIS" -f "$FD" -v "+15551234567" contacts >/dev/null
"$AIS" -f "$FD" -v "keep this"    keep     >/dev/null
"$AIS" -f "$FD" -v "tagged"       t1 t2    >/dev/null
"$AIS" -f "$FD" --update 3 -- -t2 >/dev/null          # a key detach, for ktomb
"$AIS" -f "$FD" -y --del 1 >/dev/null
okeq    "forget: the delete exports before purging"  "1" \
        "$("$AIS" -f "$FD" --export | grep -c '^D|')"
okeq    "forget: the detach exports too"             "1" \
        "$("$AIS" -f "$FD" --export | grep -c '^K|')"
"$AIS" -f "$FD" -y --compact --forget-deleted >/dev/null 2>&1
okeq    "forget: the delete no longer travels"       "0" \
        "$("$AIS" -f "$FD" --export | grep -c '^D|')"
okeq    "forget: the detach no longer travels"       "0" \
        "$("$AIS" -f "$FD" --export | grep -c '^K|')"
# the hash is what a guess is tested against; it must be gone from disk
okeq    "forget: no hash is left to test a guess against" "0" \
        "$(cut -d'|' -f3 < "$FD/tomb" | grep -c '[0-9a-f]')"
# and the deletion must still WORK here -- forgetting is not undeleting
okempty "forget: the deleted record stays deleted"   "$("$AIS" -f "$FD" contacts)"
ok      "forget: live records are untouched"         "keep this" "$("$AIS" -f "$FD" keep)"
okempty "forget: the detached tag stays detached"    "$("$AIS" -f "$FD" t2)"
ok      "forget: the record kept its other tag"      "tagged" "$("$AIS" -f "$FD" t1)"
rm -rf "$FD"

# 17s. BACKUP FIDELITY. Every round-trip test in this suite used a single-line,
#      single-value record -- "second", "alpha", "http://solo" -- so the suite was
#      green for months while --export/--dump silently DROPPED document bodies and
#      SPLIT every multi-link record into separate records. A round-trip test is
#      only as good as the SHAPES in its fixture, so this one carries all of them.
BK=$(mktemp -d "${TMPDIR:-/tmp}/ais_backup.XXXXXX") || exit 2
BR=$(mktemp -d "${TMPDIR:-/tmp}/ais_backup2.XXXXXX") || exit 2
printf 'doc line one\ndoc line two\n' | "$AIS" -f "$BK" --doc papers notes >/dev/null
"$AIS" -f "$BK" -v https://x/a -v https://x/b -v https://x/c trio >/dev/null
"$AIS" -f "$BK" -v "plain value" simple >/dev/null
"$AIS" -f "$BK" -v "untagged one" "" >/dev/null
"$AIS" -f "$BK" -v "doomed" gone >/dev/null
"$AIS" -f "$BK" -y --del 5 >/dev/null
src=$("$AIS" -f "$BK" --stats | head -1)
"$AIS" -f "$BK" --export | "$AIS" -f "$BR" --import >/dev/null 2>&1
okeq    "backup: the record COUNT survives"      "$src" "$("$AIS" -f "$BR" --stats | head -1)"
# the multi-link record must come back as ONE record, not three
okeq    "backup: a 3-link record stays one record" "3" \
        "$("$AIS" -f "$BR" trio | grep -c .)"
okeq    "backup: and all three links are on it"  "1" \
        "$("$AIS" -f "$BR" trio | cut -d'|' -f1 | sort -u | grep -c .)"
# the document's BODY has to travel, not just the pointer to it
ok      "backup: the document body is restored"  "doc line one" \
        "$(cat "$BR"/blobs/*.txt 2>/dev/null)"
ok      "backup: and its second line too"        "doc line two" \
        "$(cat "$BR"/blobs/*.txt 2>/dev/null)"
ok      "backup: the plain record survives"      "plain value" "$("$AIS" -f "$BR" simple)"
ok      "backup: the untagged record survives"   "untagged one" "$("$AIS" -f "$BR" --find untagged)"
okempty "backup: the deleted one stays deleted"  "$("$AIS" -f "$BR" gone)"
# the whole point, stated once: the two libraries must be the same library
# temp files, not `diff <(...)`: process substitution is a bashism and this suite
# runs under sh (dash), where it is a syntax error -- the same class of portability
# bug as `timeout` missing on macOS.
"$AIS" -f "$BK" --dump | cut -d'|' -f2- | sort > "$BK/a.dump"
"$AIS" -f "$BR" --dump | cut -d'|' -f2- | sort > "$BK/b.dump"
if diff "$BK/a.dump" "$BK/b.dump" >/dev/null 2>&1; then
    pass=$((pass + 1)); echo "  ok   backup: the two libraries are identical"
else
    fail=$((fail + 1)); echo "  FAIL backup: the restored library differs from the source"
fi
rm -rf "$BK" "$BR"

# 17k. --del-under is the clear name for --del-key; the old spelling keeps working
#      and says so. --del previews the record instead of just its id.
AL=$(mktemp -d "${TMPDIR:-/tmp}/ais_alias.XXXXXX") || exit 2
ALT="$AL/answer"; printf 'n\n' > "$ALT"
"$AIS" -f "$AL" -v "http://alias-me" ak >/dev/null
und=$(AIS_TTY="$ALT" "$AIS" -f "$AL" --del-under ak 2>&1)
ok      "del-under: the new name works"       "deletes RECORDS" "$und"
old=$(AIS_TTY="$ALT" "$AIS" -f "$AL" --del-key ak 2>&1)
ok      "del-key: still works as an alias"    "deletes RECORDS" "$old"
ok      "del-key: points at the new name"     "now --del-under" "$old"
ok      "del-key: points at --untag too"      "untag removes just the tag" "$old"
hlp=$("$AIS" --help 2>&1)
ok      "help: lists --untag"                 "\-\-untag KEY" "$hlp"
ok      "help: lists --del-under"             "\-\-del-under KEY" "$hlp"
# the SAFE one is listed first: the eye should land on the reversible option
okeq    "help: the safe command is listed first" "1" \
        "$(printf '%s\n' "$hlp" | grep -n -- '--untag KEY\|--del-under KEY' | head -1 | grep -c 'untag')"
okeq    "del-under: does NOT print the alias notice" "0" "$(printf '%s' "$und" | grep -c 'now --del-under')"
okeq    "del-under: reports its own name"     "1" "$(printf '%s' "$und" | grep -c -- '--del-under deletes RECORDS')"
# scanning argv for the literal "--del-key" got this wrong in both directions:
# it fired on a KEY that happened to be spelled that way, and it stayed silent
# for getopt's own unambiguous abbreviation.
abbr=$(AIS_TTY="$ALT" "$AIS" -f "$AL" --del-k ak 2>&1)
ok      "del-key: the abbreviation gets the notice too" "now --del-under" "$abbr"
lit=$(AIS_TTY="$ALT" "$AIS" -f "$AL" --del-under -- "--del-key" 2>&1)
okeq    "del-under: a KEY named --del-key is not the alias" "0" "$(printf '%s' "$lit" | grep -c 'now --del-under')"
# the positive control: grep -c 0 also passes on empty output from a crash
ok      "del-under: and it really was used as a KEY" "under '\-\-del-key'" "$lit"

dl=$(AIS_TTY="$ALT" "$AIS" -f "$AL" --del 1 2>&1)
ok      "del: previews the record, not just the id" "http://alias-me" "$dl"
ok      "del: still asks"                     "Permanently delete record 1" "$dl"
ok      "del: declining kept it"              "http://alias-me" "$("$AIS" -f "$AL" ak)"
rm -rf "$AL"

# 17. saved default index persists in ~/.ais/config ACROSS PROCESSES. Each ais
#     call is a fresh process, so reading the path back -- and resolving --where
#     to it from a dir with no local .ais -- proves it was written to disk, not
#     held in memory. Idempotent: saving the same path twice is stable.
#     --default writes the real ~/.ais/config (home is the OS account dir, not a
#     redirectable env var), so snapshot it and restore on exit.
CFG="$HOME/.ais/config"
# A run killed with SIGKILL cannot restore anything, so clear a leftover redirect
# before starting. Only ever removes a line naming a directory under /tmp that no
# longer exists -- a saved default pointing there is unusable however it got there.
if [ -f "$CFG" ]; then
    stale=$(sed -n 's/^index = \(\/tmp\/.*\)$/\1/p' "$CFG")
    if [ -n "$stale" ] && [ ! -d "$stale" ]; then
        # `|| true`: grep exits 1 when it filters EVERY line, which is exactly the
        # single-line case this repairs -- the && form silently did nothing.
        grep -vF "index = $stale" "$CFG" > "$CFG.clean" || true
        mv "$CFG.clean" "$CFG"
        echo "  note cleared a stale index redirect left by a killed run: $stale"
    fi
fi
CFGBAK="$DIR/config.orig"; HADCFG=no
[ -f "$CFG" ] && { cp "$CFG" "$CFGBAK"; HADCFG=yes; }
restore_cfg() { if [ "$HADCFG" = yes ]; then cp "$CFGBAK" "$CFG"; else rm -f "$CFG"; fi; }
# restore BEFORE removing $DIR -- the backup (CFGBAK) lives inside it. Also on a
# SIGNAL, not just a clean exit: a run killed here (a CI timeout, a ^C) used to
# leave the developer's REAL config pointing at a temp dir this suite then
# deleted, so every later `ais` call failed to open its index.
trap 'restore_cfg; rm -rf "$DIR"' EXIT
trap 'restore_cfg; rm -rf "$DIR"; exit 130' INT
trap 'restore_cfg; rm -rf "$DIR"; exit 143' TERM HUP

TGT="$DIR/saved-default"
"$AIS" --default "$TGT" >/dev/null                              # save (process A)
okeq "default: a new process reads back the saved path" "$TGT" "$("$AIS" --default)"
okeq "default: --where resolves to the saved index"     "$TGT" "$(cd "$DIR" && "$AIS" --where)"
"$AIS" --default "$TGT" >/dev/null                              # save again
okeq "default: saving the same path twice is idempotent" "$TGT" "$("$AIS" --default)"
"$AIS" --default '' >/dev/null                                  # clear
ok   "default: clearing falls back to the built-in default" "no saved default" "$("$AIS" --default)"

# 18. --export streams the merge format that --import consumes, so a pipe between
#     two indexes merges A into B locally (no network). The two live records must
#     cross over; the deleted one must NOT. The -v save returns the new record id.
EA=$(mktemp -d "${TMPDIR:-/tmp}/ais_exp_a.XXXXXX") || exit 2
EB=$(mktemp -d "${TMPDIR:-/tmp}/ais_exp_b.XXXXXX") || exit 2
"$AIS" -f "$EA" -v alpha one  >/dev/null
"$AIS" -f "$EA" -v beta  two  >/dev/null
gid=$("$AIS" -f "$EA" -v gamma three)                          # save returns the id
"$AIS" -f "$EA" -y --del "$gid" >/dev/null
"$AIS" -f "$EA" --export | "$AIS" -f "$EB" --import >/dev/null
bdump=$("$AIS" -f "$EB" --dump)
ok      "export/import: live value 'alpha' merged into B" "alpha" "$bdump"
ok      "export/import: live value 'beta' merged into B"  "beta"  "$bdump"
case "$bdump" in
    *gamma*) fail=$((fail + 1)); echo "  FAIL export/import: deleted record leaked into B" ;;
    *)       pass=$((pass + 1)); echo "  ok   export/import: deleted record absent from B" ;;
esac
rm -rf "$EA" "$EB"

# --- Regression: store integrity (found by exercising the binary, not reading) --
# The three below all SILENTLY corrupted or lost data while reporting success,
# and every one is proven to bite: revert its fix and the assertion fails.

# (1) `ais --dump | ais --import` is the documented backup/upgrade path, but
#     import took dump's "id|keys|value" as "keys|value", making the id the key
#     and folding the real keys into the value -- every record corrupted.
DI=$(mktemp -d "${TMPDIR:-/tmp}/ais_di.XXXXXX") || exit 2
DJ=$(mktemp -d "${TMPDIR:-/tmp}/ais_dj.XXXXXX") || exit 2
"$AIS" -f "$DI" -v 'hello world' foo bar >/dev/null
"$AIS" -f "$DI" --dump | "$AIS" -f "$DJ" --import >/dev/null 2>&1
ok      "dump|import: value round-trips"       "hello world" "$("$AIS" -f "$DJ" foo)"
ok      "dump|import: key 'bar' preserved"     "hello world" "$("$AIS" -f "$DJ" bar)"
# the id itself must NOT survive as a key (the corruption signature)
okempty "dump|import: id not stored as a key"  "$("$AIS" -f "$DJ" 1)"
rm -rf "$DI" "$DJ"

# (2) A '|' in a key is the store's field delimiter: stored raw, it shifted the
#     value into the wrong field, so recall returned a corrupted value.
PK=$(mktemp -d "${TMPDIR:-/tmp}/ais_pk.XXXXXX") || exit 2
"$AIS" -f "$PK" -v PAYDAY 'money|bank' >/dev/null
okeq    "pipe-in-key: value is exactly PAYDAY, not corrupted" \
        "PAYDAY" "$("$AIS" -f "$PK" 'money|bank' | sed 's/^[0-9]*|//')"
rm -rf "$PK"

# (3) An embedded newline made the value multi-line: fgets stopped at the '\n' on
#     readback and dropped the tail (silent, unrecoverable). Must be REFUSED now.
NL=$(mktemp -d "${TMPDIR:-/tmp}/ais_nl.XXXXXX") || exit 2
nlout=$("$AIS" -f "$NL" -v "$(printf 'part_A\npart_B')" note 2>&1)
ok      "newline-value: refused with a clear message" "multiple lines" "$nlout"
okempty "newline-value: nothing was stored"           "$("$AIS" -f "$NL" --dump 2>/dev/null)"
rm -rf "$NL"

# --- folder sync must never invent its target -------------------------------
#     A typo, or an unplugged drive whose mount point is an empty directory,
#     used to be silently created: every run said "synced folder", wrote a
#     bundle nobody would read, and the user believed they had a backup. The
#     failure only surfaces when the data is needed, which is too late.
FS=$(mktemp -d "${TMPDIR:-/tmp}/ais_fs.XXXXXX") || exit 2
"$AIS" -f "$FS" -v 'http://x/one' reading >/dev/null
GONE="${TMPDIR:-/tmp}/ais_fs_absent.$$"
rm -rf "$GONE"
fsout=$("$AIS" -f "$FS" --sync-folder "$GONE" 2>&1); fsrc=$?
ok      "sync-folder: a missing folder is named in the error" "no such folder" "$fsout"
okeq    "sync-folder: and it fails, loudly"                   "1" "$fsrc"
if [ -d "$GONE" ]; then gone=exists; else gone=absent; fi
okeq    "sync-folder: it did NOT create the folder"        "absent" "$gone"
printf 'x\n' > "$GONE"
fsout=$("$AIS" -f "$FS" --sync-folder "$GONE" 2>&1)
ok      "sync-folder: a plain file is refused too"            "not a folder" "$fsout"
rm -f "$GONE"; mkdir -p "$GONE"
fsout=$("$AIS" -f "$FS" --sync-folder "$GONE" 2>&1)
ok      "sync-folder: once the folder exists, it works"       "synced folder" "$fsout"
rm -rf "$FS" "$GONE"

# --- an EDIT after a remote DELETE is a later user action, and wins ----------
#     Merging compares a delete against the record's CREATION time, so re-tagging
#     something another device had deleted lost silently on the next sync: the
#     user's most recent action was the one thrown away. The "mts" sidecar
#     carries the last local edit; merge and export use the later of the two.
EA=$(mktemp -d "${TMPDIR:-/tmp}/ais_ea.XXXXXX") || exit 2
EB=$(mktemp -d "${TMPDIR:-/tmp}/ais_eb.XXXXXX") || exit 2
EF=$(mktemp -d "${TMPDIR:-/tmp}/ais_ef.XXXXXX") || exit 2
"$AIS" -f "$EA" -v 'http://x/edit-me' reading >/dev/null
"$AIS" -f "$EB" -v 'http://x/edit-me' reading >/dev/null
"$AIS" -f "$EB" --del 1 -y >/dev/null 2>&1            # the phone deletes it
sleep 1
"$AIS" -f "$EA" --update 1 important >/dev/null 2>&1  # a second later, the laptop re-tags
"$AIS" -f "$EB" --sync-folder "$EF" >/dev/null
"$AIS" -f "$EA" --sync-folder "$EF" >/dev/null
ok      "edit-vs-delete: the later edit survives the earlier remote delete" \
        "edit-me" "$("$AIS" -f "$EA" reading 2>&1)"
ok      "edit-vs-delete: the added tag came with it" \
        "edit-me" "$("$AIS" -f "$EA" important 2>&1)"
rm -rf "$EA" "$EB" "$EF"

# --- the same, through the PRIMARY save form --------------------------------
#     `ais -v VALUE KEY` on a value the index already holds is how a tag gets
#     attached, and it is what every GUI save path calls. Protecting only
#     --update/--set left the common command losing edits exactly as before.
PA=$(mktemp -d "${TMPDIR:-/tmp}/ais_pa.XXXXXX") || exit 2
PB=$(mktemp -d "${TMPDIR:-/tmp}/ais_pb.XXXXXX") || exit 2
PF=$(mktemp -d "${TMPDIR:-/tmp}/ais_pf.XXXXXX") || exit 2
"$AIS" -f "$PA" -v 'http://x/save-me' reading >/dev/null
"$AIS" -f "$PB" -v 'http://x/save-me' reading >/dev/null
"$AIS" -f "$PB" --del 1 -y >/dev/null 2>&1
sleep 1
"$AIS" -f "$PA" -v 'http://x/save-me' important >/dev/null   # re-save = attach a tag
"$AIS" -f "$PB" --sync-folder "$PF" >/dev/null
"$AIS" -f "$PA" --sync-folder "$PF" >/dev/null
ok      "put-path: a re-save after a remote delete survives" \
        "save-me" "$("$AIS" -f "$PA" reading 2>&1)"
ok      "put-path: with the tag it was saved under" \
        "save-me" "$("$AIS" -f "$PA" important 2>&1)"
rm -rf "$PA" "$PB" "$PF"

# --- an unrelated edit must NOT bring back a tag another device removed ------
#     The exported timestamp decides key attaches as well as record deletes, so
#     an edit clock that rode out on the wire resurrected deliberately removed
#     tags mesh-wide and destroyed the ktomb proving the removal.
KA=$(mktemp -d "${TMPDIR:-/tmp}/ais_ka.XXXXXX") || exit 2
KB=$(mktemp -d "${TMPDIR:-/tmp}/ais_kb.XXXXXX") || exit 2
KF=$(mktemp -d "${TMPDIR:-/tmp}/ais_kf.XXXXXX") || exit 2
"$AIS" -f "$KA" -v 'http://x/doc' work reading >/dev/null
"$AIS" -f "$KB" -v 'http://x/doc' work reading >/dev/null
sleep 1                                               # timestamps are per-second
"$AIS" -f "$KB" --update 1 -- -work >/dev/null 2>&1   # B removes a tag on purpose
sleep 1
"$AIS" -f "$KA" --update 1 important >/dev/null 2>&1  # A adds a DIFFERENT tag, later
"$AIS" -f "$KA" --sync-folder "$KF" >/dev/null
"$AIS" -f "$KB" --sync-folder "$KF" >/dev/null
"$AIS" -f "$KA" --sync-folder "$KF" >/dev/null
okempty "removed-tag: stays removed on the device that removed it" \
        "$("$AIS" -f "$KB" work 2>/dev/null)"
okempty "removed-tag: and the removal reaches the other device" \
        "$("$AIS" -f "$KA" work 2>/dev/null)"
ok      "removed-tag: while the unrelated new tag lives" \
        "doc" "$("$AIS" -f "$KA" important 2>&1)"

# --- and a tag put BACK on must propagate too --------------------------------
#     A detach used to win for ever: it was judged against the record's creation
#     time, which it always beats, so the peer's K| undid the re-attach on the
#     very device that made it. The attach now carries its own time (T|).
sleep 1
"$AIS" -f "$KA" --update 1 work >/dev/null 2>&1        # A puts the tag back, later
ok      "re-attach: the device that re-attached shows it" \
        "doc" "$("$AIS" -f "$KA" work 2>&1)"
okeq    "re-attach: and its export says when the tag went back on" \
        "1" "$("$AIS" -f "$KA" --export 2>/dev/null | grep -c '^T|.*|work$')"
for r in 1 2 3; do
    "$AIS" -f "$KA" --sync-folder "$KF" >/dev/null 2>&1
    "$AIS" -f "$KB" --sync-folder "$KF" >/dev/null 2>&1
done
ok      "re-attach: it survives the sync that used to undo it" \
        "doc" "$("$AIS" -f "$KA" work 2>&1)"
ok      "re-attach: and reaches the device that had removed it" \
        "doc" "$("$AIS" -f "$KB" work 2>&1)"
okempty "re-attach: no key tombstone is left behind" \
        "$(cat "$KA/ktomb" "$KB/ktomb" 2>/dev/null)"
rm -rf "$KA" "$KB" "$KF"

# --- a removed tag stays removed even when a DELETE is in the mix ------------
#     The record that survives a delete gets restamped, and that timestamp also
#     decides key attaches: a record coming back from a peer used to bring its
#     old keys field with it and re-advertise tags another device had removed.
DA=$(mktemp -d "${TMPDIR:-/tmp}/ais_da.XXXXXX") || exit 2
DB=$(mktemp -d "${TMPDIR:-/tmp}/ais_db.XXXXXX") || exit 2
DF=$(mktemp -d "${TMPDIR:-/tmp}/ais_df.XXXXXX") || exit 2
"$AIS" -f "$DA" -v 'http://x/doc2' work reading >/dev/null
"$AIS" -f "$DB" -v 'http://x/doc2' work reading >/dev/null
sleep 1
"$AIS" -f "$DA" --del 1 -y >/dev/null 2>&1              # A deletes
sleep 1
"$AIS" -f "$DB" --update 1 -- -work >/dev/null 2>&1     # B removes a tag
sleep 1
"$AIS" -f "$DB" --update 1 extra >/dev/null 2>&1        # B edits, so B's copy wins
for r in 1 2 3; do
    "$AIS" -f "$DA" --sync-folder "$DF" >/dev/null 2>&1
    "$AIS" -f "$DB" --sync-folder "$DF" >/dev/null 2>&1
done
ok      "delete-conflict: the edited record wins"        "doc2" "$("$AIS" -f "$DA" extra 2>&1)"
okempty "delete-conflict: the removed tag stays removed" "$("$AIS" -f "$DB" work 2>/dev/null)"
okempty "delete-conflict: and does not come back on the other device" \
        "$("$AIS" -f "$DA" work 2>/dev/null)"
okeq    "delete-conflict: the survivor exports its true time beside the raise" \
        "1" "$("$AIS" -f "$DB" --export 2>/dev/null | grep -c '^C|')"
rm -rf "$DA" "$DB" "$DF"

# --- but a delete NEWER than the edit must still win ------------------------
#     The sidecar must not make an edited record undeletable from another device.
LA=$(mktemp -d "${TMPDIR:-/tmp}/ais_la.XXXXXX") || exit 2
LB=$(mktemp -d "${TMPDIR:-/tmp}/ais_lb.XXXXXX") || exit 2
LF=$(mktemp -d "${TMPDIR:-/tmp}/ais_lf.XXXXXX") || exit 2
"$AIS" -f "$LA" -v 'http://x/kill-me' reading >/dev/null
"$AIS" -f "$LB" -v 'http://x/kill-me' reading >/dev/null
"$AIS" -f "$LA" --update 1 important >/dev/null 2>&1  # the laptop edits ...
sleep 1
"$AIS" -f "$LB" --del 1 -y >/dev/null 2>&1            # ... and THEN the phone deletes
"$AIS" -f "$LB" --sync-folder "$LF" >/dev/null
"$AIS" -f "$LA" --sync-folder "$LF" >/dev/null
okempty "later-delete: a delete newer than the edit still removes the record" \
        "$("$AIS" -f "$LA" reading 2>/dev/null)"
rm -rf "$LA" "$LB" "$LF"

# --- an export with more deletes than one import batch holds -----------------
#     A run of D| lines is resolved a batch at a time (ais_merge_del_many), so a
#     peer that has deleted a lot exercises the flush at the buffer boundary AND
#     at end of stream. Every delete must still land, and the live records stay.
BA=$(mktemp -d "${TMPDIR:-/tmp}/ais_bba.XXXXXX") || exit 2
BB=$(mktemp -d "${TMPDIR:-/tmp}/ais_bbb.XXXXXX") || exit 2
i=1; while [ $i -le 300 ]; do echo "http://x/gone-$i"; i=$((i + 1)); done > "$BA/doomed.txt"
printf 'http://x/keep-1\nhttp://x/keep-2\n' > "$BA/alive.txt"
for d in "$BA" "$BB"; do
    "$AIS" -f "$d" -v - doomed < "$BA/doomed.txt" >/dev/null 2>&1
    "$AIS" -f "$d" -v - alive  < "$BA/alive.txt"  >/dev/null 2>&1
done
"$AIS" -f "$BA" --del-under doomed -y >/dev/null 2>&1
"$AIS" -f "$BA" --export > "$BA/stream" 2>/dev/null
okeq    "batch: the export carries 300 deletes" "300" "$(grep -c '^D|' "$BA/stream")"
"$AIS" -f "$BB" --import < "$BA/stream" >/dev/null 2>&1
okempty "batch: every delete crossed into the peer" "$("$AIS" -f "$BB" doomed 2>/dev/null)"
ok      "batch: the live records are untouched" "keep-2" "$("$AIS" -f "$BB" alive 2>/dev/null)"
rm -rf "$BA" "$BB"

# --- an over-long import line must not fabricate a record from its tail --------
#     fgets returns an over-long line as TWO: the head was refused as "too long"
#     and the TAIL was then parsed as a normal record. Everything past offset
#     65535 is chosen by whoever wrote the file, so importing one bad line
#     silently created a record with forged keys and a forged value, while the
#     record the user meant to import was dropped. Found by an adversarial pass,
#     not by any existing test.
OL=$(mktemp -d "${TMPDIR:-/tmp}/ais_ol.XXXXXX") || exit 2
{ printf 'mytag|'; head -c 65529 /dev/zero | tr '\0' 'A'
  printf '9999|2026-01-01T00:00:00Z|forgedkeys|forgedvalue\n'; } > "$OL/long"
olout=$("$AIS" -f "$OL" --import < "$OL/long" 2>&1)
ok      "longline: the whole line is refused, once"  "skipped whole"  "$olout"
ok      "longline: and nothing was imported"         "imported 0"     "$olout"
okempty "longline: no record was fabricated"         "$("$AIS" -f "$OL" forgedkeys 2>/dev/null)"
okempty "longline: the store stayed empty"           "$("$AIS" -f "$OL" --dump 2>/dev/null)"
# the interactive variant shares the reader and so shared the defect
printf 'y\ny\n' > "$OL/ans"
oli=$(AIS_TTY="$OL/ans" "$AIS" -f "$OL" --import-interactively < "$OL/long" 2>&1)
okempty "longline: --import-interactively is not fooled either" \
        "$("$AIS" -f "$OL" forgedkeys 2>/dev/null)"
rm -rf "$OL"

# --- --import-interactively: the per-record gate, never driven until now -------
#     The use case is a knowledge base handed from one person to another, where
#     the acceptor keeps some records and drops others. The DEFAULT decides how
#     that goes: a bare Enter SKIPS (feed.c takes only y/Y), so someone holding
#     Enter through a colleague's index imports nothing, and learns it only from
#     the final count. Nothing pinned that, and the two possible defaults differ
#     by "took everything" against "took nothing" -- so a silent flip either way
#     is the dangerous kind.
II=$(mktemp -d "${TMPDIR:-/tmp}/ais_ii.XXXXXX") || exit 2
printf 'k1|http://take-me\nk2|http://skip-me\nk3|http://take-me-too\n' > "$II/stream"
printf 'y\nn\ny\n' > "$II/ans"
iiout=$(AIS_TTY="$II/ans" "$AIS" -f "$II" --import-interactively < "$II/stream" 2>&1)
ok      "import-i: reports how many of how many"   "imported 2 of 3" "$iiout"
ok      "import-i: an accepted record is stored"   "http://take-me"  "$("$AIS" -f "$II" k1)"
ok      "import-i: the second accepted one too"    "http://take-me-too" "$("$AIS" -f "$II" k3)"
okempty "import-i: a refused record is NOT stored" "$("$AIS" -f "$II" k2 2>/dev/null)"
# the prompt must show the record before asking, or the choice is blind
ok      "import-i: shows the record before asking" "take into your index" "$iiout"
# THE DEFAULT, pinned: bare Enter must skip, matching the [y/N] the prompt shows
II2=$(mktemp -d "${TMPDIR:-/tmp}/ais_ii2.XXXXXX") || exit 2
printf '\n\n\n' > "$II2/ans"
ii2=$(AIS_TTY="$II2/ans" "$AIS" -f "$II2" --import-interactively < "$II/stream" 2>&1)
ok      "import-i: bare Enter skips (the [y/N] default)" "imported 0 of 3" "$ii2"
okempty "import-i: and nothing was stored"         "$("$AIS" -f "$II2" k1 2>/dev/null)"
rm -rf "$II" "$II2"

echo "---- $pass passed, $fail failed"
[ "$fail" -eq 0 ]
