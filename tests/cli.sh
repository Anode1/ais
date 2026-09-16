#!/bin/sh
# cli.sh -- end-to-end tests of the ais BINARY: the streaming stdin path (`-v -`),
# pipelines, argv handling, exit codes -- what the C unit tests cannot reach.
# POSIX sh, so it runs unchanged on Linux and macOS.
#
# Grammar: bare args are KEYS; -v marks a value; --word is a command.
#
# Usage:  sh tests/cli.sh [path-to-ais]      (default ./c/ais)

AIS=${1:-./c/ais}
# Absolute path: tests cd into temp dirs, where a relative ./c/ais would not resolve.
case $AIS in
    /*) ;;
    *)  AIS=$(cd "$(dirname "$AIS")" && pwd)/$(basename "$AIS") ;;
esac
pass=0
fail=0

# `timeout` guards the untag cases so a lost loop-guard FAILS instead of hanging.
# macOS ships it as gtimeout from coreutils, or not at all; run unguarded there.
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

keys=$("$AIS" -f "$DIR" --keys)
ok    "stdin: key 'greek' is in the database" "greek" "$keys"

# 2. self-index: pipe the binary's own path in; recall it by key and by content.
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
mkdir "$II/idx"              # -f refuses to create a missing DIR
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
# --dump emits the import grammar, so dump|import round-trips with no sed
IM2=$(mktemp -d "${TMPDIR:-/tmp}/ais_import2.XXXXXX") || exit 2
"$AIS" -f "$IM" --dump | "$AIS" -f "$IM2" --import 2>/dev/null
out=$("$AIS" -f "$IM2" a b)
ok "import: dump|import round-trips"    "second" "$out"
rm -rf "$IM" "$IM2"

# 8. doc: a multi-line document becomes a blob file; recall cats its content.
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
# 1 live (the two links are one record), 1 deleted, 2 keys filed until compaction.
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
"$AIS" -f "$CP" -v survivor scratch >/dev/null   # a store wiped to nothing must fail
"$AIS" -f "$CP" -y --del "$cid" >/dev/null
"$AIS" -f "$CP" -y --compact >/dev/null
ok "compact: the live record is still there" "survivor" "$(cat "$CP"/store)"
ok "compact: and still answers by key"       "survivor" "$("$AIS" -f "$CP" scratch)"
case "$(cat "$CP"/store)" in
    *doomed*) fail=$((fail + 1)); echo "  FAIL compact: deleted value still in store" ;;
    *)        pass=$((pass + 1)); echo "  ok   compact: deleted record physically gone" ;;
esac
rm -rf "$CP"

# 15. concurrency: two parallel writers never collide on an id (per-op write lock).
CC=$(mktemp -d "${TMPDIR:-/tmp}/ais_cc.XXXXXX") || exit 2
( i=0; while [ $i -lt 50 ]; do "$AIS" -f "$CC" -v "a$i" w1 >/dev/null; i=$((i+1)); done ) &
( i=0; while [ $i -lt 50 ]; do "$AIS" -f "$CC" -v "b$i" w2 >/dev/null; i=$((i+1)); done ) &
wait
total=$(grep -c . "$CC"/store)
uniq=$(cut -d'|' -f1 "$CC"/store | sort -un | grep -c .)
okeq "concurrency: 100 records written"            "100" "$total"
okeq "concurrency: all ids unique (no collision)"  "$total" "$uniq"
rm -rf "$CC"

# 16. --timeline lists newest first, a dateless record included; --tags counts keys.
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

# 17b. --update edits a record's keys by id: -KEY detaches, KEY attaches.
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
# export reads the STORE, so a key posted only to idx/ never reaches a peer.
UPD=$(mktemp -d "${TMPDIR:-/tmp}/ais_updpeer.XXXXXX") || exit 2
"$AIS" -f "$UP" --export > "$UPD/stream" 2>/dev/null
"$AIS" -f "$UPD" --import < "$UPD/stream" >/dev/null 2>&1
ok      "update: attached key reaches a peer"       "venice" "$("$AIS" -f "$UPD" tuscany)"
rm -rf "$UPD"
rm -rf "$UP"

# 17c. --set replaces ONE of a multi-link record's values in place.
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
# a multi-line new value goes out-of-line: the record points at a fresh document
# blob (never a raw newline in the store), and reads back whole.
"$AIS" -f "$SV" --set "$sid" -v "https://doi.org/10.5281/zenodo.110" -v "$(printf 'a\nb')" 2>/dev/null
ok      "set: a multi-line value becomes a document" "$(printf 'a\nb')" "$("$AIS" -f "$SV" papers | sed 's/^[0-9]*|//')"
okeq    "set: the store is still one line per link" "2" "$(grep -c . "$SV/store")"
ok      "set: the store holds a blob path, not the text" "blobs/" "$(grep -o 'blobs/' "$SV/store" | head -1)"
# and back: a one-line edit of the document returns inline and retires the blob
"$AIS" -f "$SV" --set "$sid" -v "$(grep -o 'blobs/[^|]*' "$SV/store" | head -1)" -v "https://doi.org/10.5281/zenodo.112" 2>/dev/null
ok      "set: a one-line edit of a document comes back inline" "zenodo.112" "$("$AIS" -f "$SV" papers)"
okeq    "set: the retired blob file is gone" "0" "$(ls "$SV/blobs" 2>/dev/null | grep -c .)"
rm -rf "$SV"

# 17d. An attached key is folded before it reaches the store line ('|' and control
#      bytes -> '_'): a raw '|' would shift the value into the wrong field, and a
#      raw newline would end the line and drop the value.
FK=$(mktemp -d "${TMPDIR:-/tmp}/ais_fold.XXXXXX") || exit 2
fid=$("$AIS" -f "$FK" -v "http://rome" italy)
"$AIS" -f "$FK" --update "$fid" 'a|b' >/dev/null 2>&1
ok      "fold: the value survives a '|' in an attached key" "http://rome" "$("$AIS" -f "$FK" italy)"
ok      "fold: the key is stored folded"                    "a_b"         "$("$AIS" -f "$FK" --dump)"
"$AIS" -f "$FK" --update "$fid" "$(printf 'x\ny')" >/dev/null 2>&1
ok      "fold: the value survives a newline in an attached key" "http://rome" "$("$AIS" -f "$FK" italy)"
okeq    "fold: the store is still a single line"            "1" "$(grep -c . "$FK/store")"
rm -rf "$FK"

# 17e. The store is written before the index. A rewrite that cannot fit must leave
#      no posting behind: the next --compact drops a key the store does not know.
AT=$(mktemp -d "${TMPDIR:-/tmp}/ais_atom.XXXXXX") || exit 2
big=$(printf 'v%.0s' $(seq 1 65400))
longkey=$(printf 'z%.0s' $(seq 1 200))
aid=$("$AIS" -f "$AT" -v "$big" only)
"$AIS" -f "$AT" --update "$aid" alpha "$longkey" >/dev/null 2>&1   # 'alpha' fits, the pair does not
okeq    "atomic: the keys field is unchanged when the rewrite cannot fit" "only" "$(cut -d'|' -f3 "$AT/store")"
okempty "atomic: no phantom posting for the rejected keys"  "$(find "$AT/idx" -name 'alpha' -o -name "$longkey")"
rm -rf "$AT"

# 17f. --set refuses what breaks value-identity: a deleted id, and a value another
#      record holds (two records sharing a value make a peer collapse them).
GD=$(mktemp -d "${TMPDIR:-/tmp}/ais_guard.XXXXXX") || exit 2
d1=$("$AIS" -f "$GD" -v "http://one" g1)
d2=$("$AIS" -f "$GD" -v "http://two" g2)
"$AIS" -f "$GD" --set "$d2" -v "http://two" -v "http://one" 2>/dev/null
ok      "set: refuses a value another record holds" "http://two" "$("$AIS" -f "$GD" g2)"
"$AIS" -f "$GD" -y --del "$d1" >/dev/null 2>&1
"$AIS" -f "$GD" --set "$d1" -v "http://one" -v "http://three" 2>/dev/null
okempty "set: refuses a deleted id"                 "$("$AIS" -f "$GD" --find http://three)"
rm -rf "$GD"

# 17g. Encode-equivalent spellings are ONE key: idx/ holds a single entry for
#      "Doc"/"doc"/"DOC". Separate tokens would grow the keys field on every re-put.
CS=$(mktemp -d "${TMPDIR:-/tmp}/ais_case.XXXXXX") || exit 2
"$AIS" -f "$CS" -v "CASEVAL" Doc >/dev/null
for k in doc DOC dOc DoC; do "$AIS" -f "$CS" -v "CASEVAL" $k >/dev/null; done
okeq    "case: one token kept, first spelling wins" "Doc" "$(cut -d'|' -f3 "$CS/store")"
ok      "case: recall still works by any spelling"  "CASEVAL" "$("$AIS" -f "$CS" DOC)"
rm -rf "$CS"

# 17h. A detach sticks through compaction whatever the spelling: ktomb compares the
#      key_encode() form, so detaching "doc" removes a stored "Doc". The encoding
#      also folds the '|' that would split the ktomb line's own fields.
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
# the records survive: Doc was the only key, so it is unreachable, not deleted.
ok      "detach: the record itself survives"                  "DETVAL"  "$("$AIS" -f "$DT" --find DETVAL)"
ok      "detach: the '|' record survives too"                 "PIPEVAL" "$("$AIS" -f "$DT" --find PIPEVAL)"
rm -rf "$DT"

# 17i. --del-key shows what it destroys before asking: the RECORDS, not the tag.
DK=$(mktemp -d "${TMPDIR:-/tmp}/ais_delkey.XXXXXX") || exit 2
"$AIS" -f "$DK" -v "http://keep-a" proj alpha >/dev/null
"$AIS" -f "$DK" -v "http://keep-b" proj beta  >/dev/null
"$AIS" -f "$DK" -v "http://other"  unrelated  >/dev/null
# the answer comes from AIS_TTY; stdin must not be able to answer the prompt.
DKT="$DK/answer"; printf 'n\n' > "$DKT"
prev=$(AIS_TTY="$DKT" "$AIS" -f "$DK" --del-key proj 2>&1)
ok      "del-key: names the key it would empty"    "Filed under 'proj'" "$prev"
ok      "del-key: says records, not the tag"       "deletes RECORDS, not the tag" "$prev"
ok      "del-key: lists what would go"             "http://keep-a" "$prev"
ok      "del-key: offers the non-destructive path" "ais \-\-untag proj" "$prev"
ok      "del-key: warns about the other keys"      "every other key" "$prev"
ok      "del-key: the prompt repeats the count"    "these 2 record" "$prev"
okeq    "del-key: declining deletes nothing"       "2" "$("$AIS" -f "$DK" proj | grep -c .)"
# AIS_TTY is a test seam: a destructive command must say when a file answered.
ok      "del-key: says the answer came from AIS_TTY" "reading the answer from AIS_TTY" "$prev"
AIS_TTY="$DKT" "$AIS" -f "$DK" --del-key proj >/dev/null 2>&1
okeq    "del-key: declining exits non-zero"        "1" "$?"
# a redirected data file must NOT be able to answer the prompt
printf 'yes I really do\n' > "$DK/data"
AIS_TTY=/dev/null "$AIS" -f "$DK" --del-key proj < "$DK/data" >/dev/null 2>&1
okeq    "del-key: an empty terminal answer exits 2" "2" "$?"
# /dev/null only proves the EOF branch: pit terminal against stdin, both ways, so
# only the terminal's answer decides.
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
printf 'yolo\n' > "$DKT"
AIS_TTY="$DKT" "$AIS" -f "$DK" --del-key proj >/dev/null 2>&1
okeq    "del-key: 'yolo' does not confirm"         "2" "$("$AIS" -f "$DK" proj | grep -c .)"
ok      "del-key: the unrelated record is untouched" "http://other" "$("$AIS" -f "$DK" unrelated)"
none=$(printf 'n\n' | "$AIS" -f "$DK" --del-key nosuchkey 2>&1)
ok      "del-key: an unused key asks nothing"      "nothing is filed" "$none"
"$AIS" -f "$DK" -y --del-key proj >/dev/null 2>&1
okempty "del-key: -y deletes without prompting"    "$("$AIS" -f "$DK" proj)"
ok      "del-key: -y left other records alone"     "http://other" "$("$AIS" -f "$DK" unrelated)"
rm -rf "$DK"

# 17i-bis. The manifest's shape, and that it goes to stderr, never stdout.
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

# 17i-quater. --dedupe-docs: the pre-0.3.20 clash copies, shown, then deleted.
DD=$(mktemp -d "${TMPDIR:-/tmp}/ais_dedupe.XXXXXX") || exit 2
"$AIS" -f "$DD" --init >/dev/null
mkdir -p "$DD/blobs"
printf 'one\ntwo\n' > "$DD/blobs/2020-01-01-000000.txt"
cp "$DD/blobs/2020-01-01-000000.txt" "$DD/blobs/2020-01-01-000000-1.txt"
cp "$DD/blobs/2020-01-01-000000.txt" "$DD/blobs/2020-01-01-000000-1-1.txt"
printf 'other\n' > "$DD/blobs/2020-01-01-000000-2.txt"
for b in 2020-01-01-000000.txt 2020-01-01-000000-1.txt 2020-01-01-000000-1-1.txt 2020-01-01-000000-2.txt; do
    "$AIS" -f "$DD" -v "blobs/$b" paper >/dev/null
done
printf 'n\n' > "$DD/ans"
derr=$(AIS_TTY="$DD/ans" "$AIS" -f "$DD" --dedupe-docs 2>&1 >/dev/null)
ok      "dedupe-docs: the list names the keeper"    "keep 1|blobs/2020-01-01-000000.txt" "$derr"
ok      "dedupe-docs: and each copy"                "drop 3|blobs/2020-01-01-000000-1-1.txt" "$derr"
okeq    "dedupe-docs: a different body is not listed" "0" "$(printf '%s' "$derr" | grep -c -- '-2.txt')"
ok      "dedupe-docs: n aborts"                     "aborted" "$derr"
okeq    "dedupe-docs: nothing deleted on n"         "4" "$("$AIS" -f "$DD" --dump | grep -c blobs/)"
ok      "dedupe-docs: -y deletes the copies"        "deleted 2" "$("$AIS" -f "$DD" --dedupe-docs -y 2>/dev/null)"
okeq    "dedupe-docs: the keeper and the other body stay" "2" "$("$AIS" -f "$DD" --dump | grep -c blobs/)"
okempty "dedupe-docs: the copies' blobs are gone"   "$(ls "$DD/blobs" | grep -- '-1')"
ok      "dedupe-docs: a second run finds nothing"   "no document is a copy" "$("$AIS" -f "$DD" --dedupe-docs -y 2>&1)"
rm -rf "$DD"

# 17i-ter. The prompt's own counts and its singular form, not just the result line.
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

# 17i-quater. confirm() accepts exactly y/Y/yes/YES and nothing else.
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

# 17i-quinquies. --del-under shreds encrypted blobs before tombstoning, as --del
#      does: the ciphertext must not outlive the record. The blob reference is
#      written by hand, so this needs no crypto module in the build.
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

# 17j. --untag removes the TAG and keeps the records. A record filed elsewhere
#      stays reachable there, and the removal survives compaction (a ktomb entry).
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
"$AIS" -f "$UT" -v "http://u-one" proj >/dev/null
ok      "untag: re-tagging restores it"       "http://u-one" "$("$AIS" -f "$UT" proj)"
printf 'n\n' > "$UTT"
AIS_TTY="$UTT" "$AIS" -f "$UT" --untag deploy >/dev/null 2>&1
okeq    "untag: declining exits non-zero"     "1" "$?"
ok      "untag: declining changes nothing"    "http://u-one" "$("$AIS" -f "$UT" deploy)"
none=$(AIS_TTY="$UTT" "$AIS" -f "$UT" --untag nosuch 2>&1)
ok      "untag: an unused key asks nothing"   "nothing is filed" "$none"
rm -rf "$UT"

# 17j-bis. A posting still lists ids whose records are tombstoned (removal is
#      physical only at compaction); untag must not treat those as a failure.
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
# More than one batch of 64, with deletions on the boundary: 3 records already
# exist, so bulkkey's posting holds ids 4..83 and batch one ends at id 67. 67 and
# 68 are the last of batch one and the first of batch two.
i=1; while [ $i -le 80 ]; do "$AIS" -f "$UD" -v "http://bulk$i" bulkkey >/dev/null; i=$((i+1)); done
for d in 67 68; do "$AIS" -f "$UD" -y --del "$d" >/dev/null 2>&1; done
bulk=$(tmo 30 "$AIS" -f "$UD" -y --untag bulkkey 2>&1); brc=$?
okeq    "untag: works across the batch boundary" "0" "$brc"
# the count is asserted: a miscount in the second batch is otherwise invisible
ok      "untag: counts every live record across both batches" "untagged 78$" "$bulk"
okempty "untag: nothing left under the bulk key" "$("$AIS" -f "$UD" bulkkey)"
rm -rf "$UD"

# 17j-ter. --untag can leave a record with NO keys; `--dump | --import` keeps it.
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
# A three-field line keeps its leading field as a KEY, not an id, so a year tag
# such as "2023|italy|http://photo" files under 2023.
mk=$(printf 'mykey|http://a|b\n' | "$AIS" -f "$KR" --import 2>&1)
ok      "keyless: a hand-written first field is a KEY"  "mykey" "$("$AIS" -f "$KR" --keys)"
KY=$(mktemp -d "${TMPDIR:-/tmp}/ais_keyless4.XXXXXX") || exit 2
yr=$(printf '2023|italy|http://photo\n' | "$AIS" -f "$KY" --import 2>&1)
ok      "keyless: a numeric first field survives as a key too" "2023" "$("$AIS" -f "$KY" --keys)"
ok      "keyless: and the year recalls the record" "http://photo" "$("$AIS" -f "$KY" 2023)"
rm -rf "$KY"
# A keyless record is spelled "-v VALUE". An old "|value" line is read as keyless
# with a pre-v2 warning: visible noise beats silent loss. See doc/dev/FORMAT_V2.md.
KT=$(mktemp -d "${TMPDIR:-/tmp}/ais_keyless3.XXXXXX") || exit 2
typo=$(printf '|http://typo\n' | "$AIS" -f "$KT" --import 2>&1)
ok      "keyless: an old '|value' line warns that the format changed" "pre-v2" "$typo"
ok      "keyless: and is kept rather than dropped"  "http://typo" "$("$AIS" -f "$KT" --dump)"
nk=$(printf -- '-v http://deliberate\n' | "$AIS" -f "$KT" --import 2>&1)
ok      "keyless: '-v VALUE' states keyless outright"  "imported" "$nk"
ok      "keyless: and stores it"  "http://deliberate" "$("$AIS" -f "$KT" --dump)"
rm -rf "$KT"
# A future merge verb must be refused, not turned into a record, at one character
# or two. A full ISO date in field 2 is what tells it from a real tag.
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

# 17j-quater. A key with whitespace folds to the posting's name ('a b' -> 'a_b'),
#      and the walk advances a cursor so no id can spin the loop.
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

# 17j-quinquies. Untagging a tombstoned record must still record the detach, or
#      resurrecting the record brings the tag back at the next compaction.
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

# 17j-sexies. A posting can name an id with NO store line (a hand-edited index).
#      Untag must skip it, not abort part-way and wedge the key.
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

# 17j-sexies-bis. A posting can hold a non-positive id (a hand edit, a truncated
#      write). The collection cursor starts below every id, so those are pruned.
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

# 17j-sexies-ter. A posting duplicated by a hand edit is counted once.
DP=$(mktemp -d "${TMPDIR:-/tmp}/ais_duppost.XXXXXX") || exit 2
"$AIS" -f "$DP" -v d1 dk >/dev/null
"$AIS" -f "$DP" -v d2 dk >/dev/null
dpost=$(ls "$DP"/idx/*/dk)
printf '1\n' >> "$dpost"
ok      "untag: a duplicated posting entry is counted once" "untagged 2$" \
        "$(tmo 20 "$AIS" -f "$DP" -y --untag dk 2>&1)"
rm -rf "$DP"

# 17j-septies. --del-under re-stamps an already-deleted record, so a peer add dated
#      between the two deletes stays suppressed, but does not count it.
RC=$(mktemp -d "${TMPDIR:-/tmp}/ais_restamp.XXXXXX") || exit 2
"$AIS" -f "$RC" -v c1 ck >/dev/null
"$AIS" -f "$RC" -v c2 ck >/dev/null
"$AIS" -f "$RC" -y --del 1 >/dev/null
ok      "del-under: counts only the LIVE records"     "deleted 1$" \
        "$("$AIS" -f "$RC" -y --del-under ck 2>&1)"
# The re-stamp replaces rather than accumulates: a duplicate costs every peer a
# full store scan on each future import.
okeq    "del-under: re-stamping does not grow the tomb" "1" "$(grep -c '^1|' "$RC"/tomb)"
okempty "del-under: and the record stays deleted"       "$("$AIS" -f "$RC" ck)"
rm -rf "$RC"

# 17j-octies. --del on an already-deleted id must not offer to delete it again.
DD=$(mktemp -d "${TMPDIR:-/tmp}/ais_deldead.XXXXXX") || exit 2
"$AIS" -f "$DD" -v dv1 dk >/dev/null
"$AIS" -f "$DD" -y --del 1 >/dev/null
printf 'y\n' > "$DD/ans"
dead=$(AIS_TTY="$DD/ans" "$AIS" -f "$DD" --del 1 2>&1); drc=$?
ok      "del: an already-deleted id says so"          "no live record 1" "$dead"
okeq    "del: and exits non-zero without asking"      "1" "$drc"
okeq    "del: no second tombstone was appended"       "1" "$(grep -c '^1|' "$DD"/tomb)"
rm -rf "$DD"

# 17j-nonies. The small refusals: a missing KEY is not a no-op success, declining
#      --compact does not report success, --set retires the value it replaced.
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
# --set rewrites the line in place and the record stays one live record.
"$AIS" -f "$MS" --set 1 -v m1 -v m2 >/dev/null 2>&1
ok      "set: the record reads back edited"             "m2" "$("$AIS" -f "$MS" mk)"
okeq    "set: and is still one live record"             "1"  "$("$AIS" -f "$MS" mk | grep -c .)"
ok      "set: the edit is recorded for other devices"   "|m2$" "$(cat "$MS/edits" 2>/dev/null)"
ok      "set: and leads the export, before the records" "^E|" "$("$AIS" -f "$MS" --export 2>/dev/null | head -1)"
rm -rf "$MS"

# 17m. A value the index once deleted must be saveable again, and the re-save must
#      survive the next sync. Dated lines, not sleeps, so the test is deterministic.
#      The digest is salted with the record's creation ts, so it is learnt from a
#      copy created at the same instant -- the dated 2020 line.
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
# the exported add must be NEWER than the delete, or the next sync kills it
newts=$("$AIS" -f "$RA" --export | grep 'http://x/readd' | cut -d'|' -f2)
if [ -n "$newts" ] && [ "$newts" \> "2020-06-01T00:00:00Z" ]; then
    pass=$((pass + 1)); echo "  ok   readd: it exports NEWER than the delete it survived"
else
    fail=$((fail + 1)); echo "  FAIL readd: exported ts '$newts' does not outrank the delete"
fi
printf 'D|2020-06-01T00:00:00Z|%s\n' "$hash" | "$AIS" -f "$RA" --import >/dev/null 2>&1
ok      "readd: a stale delete no longer kills it"  "http://x/readd" "$("$AIS" -f "$RA" reading)"
rm -rf "$RA"

# 17n. A tag whose every record is deleted stops being offered. The posting keeps a
#      deleted record's id until compaction, and on a phone compaction never runs.
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
"$AIS" -f "$PT" -v p9 parents >/dev/null
ok      "phantom: it returns when a live record uses it" "parents" "$("$AIS" -f "$PT" --keys)"
ok      "phantom: and counts 1, not 3"               "1  parents" "$("$AIS" -f "$PT" --tags)"
rm -rf "$PT"

# 17o. A long-running process must see records written by another process: the id
#      counter is cached at open and the timeline's ceiling comes from it.
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

# 17p. A compaction killed by SIGKILL/OOM/power cut leaves a half-built idx/ live
#      and the good tree orphaned in idx.bak. get() reads idx/ with no store
#      fallback, so keyed lookups would silently return a subset: the next open
#      must heal it. The crashed state is built by hand, for determinism and speed.
CR=$(mktemp -d "${TMPDIR:-/tmp}/ais_crashcompact.XXXXXX") || exit 2
i=1; while [ $i -le 40 ]; do "$AIS" -f "$CR" -v "rec-$i" bulk >/dev/null; i=$((i+1)); done
want=$("$AIS" -f "$CR" bulk | grep -c .)
okeq    "crash-compact: baseline recall"          "40" "$want"
# exactly what a killed run leaves behind: good tree staged, live tree half-built
mv "$CR/idx" "$CR/idx.bak"
mkdir -p "$CR/idx"
# check the crashed shape on the FILESYSTEM: opening the index triggers recovery,
# so a query through ais would heal it before it could observe it
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

# 17q. --compact --forget-deleted makes a deletion final here. A tombstone keeps an
#      FNV-1a hash of the deleted value, exported to every peer and kept for the
#      life of the index: a permanent, testable trace of what was deleted.
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

# 17s. Backup fidelity. A round-trip test is only as good as the shapes in its
#      fixture, so this one carries a document, a multi-link record, a plain one,
#      an untagged one and a deleted one.
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
okeq    "backup: a 3-link record stays one record" "3" \
        "$("$AIS" -f "$BR" trio | grep -c .)"
okeq    "backup: and all three links are on it"  "1" \
        "$("$AIS" -f "$BR" trio | cut -d'|' -f1 | sort -u | grep -c .)"
ok      "backup: the document body is restored"  "doc line one" \
        "$(cat "$BR"/blobs/*.txt 2>/dev/null)"
ok      "backup: and its second line too"        "doc line two" \
        "$(cat "$BR"/blobs/*.txt 2>/dev/null)"
ok      "backup: the plain record survives"      "plain value" "$("$AIS" -f "$BR" simple)"
ok      "backup: the untagged record survives"   "untagged one" "$("$AIS" -f "$BR" --find untagged)"
okempty "backup: the deleted one stays deleted"  "$("$AIS" -f "$BR" gone)"
# temp files, not `diff <(...)`: process substitution is a bashism and this suite
# runs under dash, where it is a syntax error
"$AIS" -f "$BK" --dump | sort > "$BK/a.dump"
"$AIS" -f "$BR" --dump | sort > "$BK/b.dump"
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
okeq    "help: the safe command is listed first" "1" \
        "$(printf '%s\n' "$hlp" | grep -n -- '--untag KEY\|--del-under KEY' | head -1 | grep -c 'untag')"
okeq    "del-under: does NOT print the alias notice" "0" "$(printf '%s' "$und" | grep -c 'now --del-under')"
okeq    "del-under: reports its own name"     "1" "$(printf '%s' "$und" | grep -c -- '--del-under deletes RECORDS')"
# the alias notice must follow getopt: fire on the abbreviation --del-k, stay
# silent on a KEY spelled "--del-key"
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

# 17. The saved default index persists in ~/.ais/config across processes; saving
#     the same path twice is idempotent. --default writes the REAL ~/.ais/config
#     (home is the OS account dir, not a redirectable env var), so this section
#     snapshots it and restores it on exit.
CFG="$HOME/.ais/config"
# A run killed with SIGKILL restores nothing, so clear a leftover redirect first.
# Only a line naming a /tmp directory that no longer exists is removed.
if [ -f "$CFG" ]; then
    stale=$(sed -n 's/^index = \(\/tmp\/.*\)$/\1/p' "$CFG")
    if [ -n "$stale" ] && [ ! -d "$stale" ]; then
        # `|| true`: grep exits 1 when it filters EVERY line, the case repaired here.
        grep -vF "index = $stale" "$CFG" > "$CFG.clean" || true
        mv "$CFG.clean" "$CFG"
        echo "  note cleared a stale index redirect left by a killed run: $stale"
    fi
fi
CFGBAK="$DIR/config.orig"; HADCFG=no
[ -f "$CFG" ] && { cp "$CFG" "$CFGBAK"; HADCFG=yes; }
restore_cfg() { if [ "$HADCFG" = yes ]; then cp "$CFGBAK" "$CFG"; else rm -f "$CFG"; fi; }
# Restore BEFORE removing $DIR -- CFGBAK lives inside it -- and on a signal too,
# or a killed run leaves the real config pointing at a temp dir this suite deletes.
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

# 18. --export streams the merge format --import consumes: a pipe merges A into B.
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

# --- Regression: store integrity -----------------------------------------------

# (1) `ais --dump | ais --import` is the documented backup/upgrade path.
DI=$(mktemp -d "${TMPDIR:-/tmp}/ais_di.XXXXXX") || exit 2
DJ=$(mktemp -d "${TMPDIR:-/tmp}/ais_dj.XXXXXX") || exit 2
"$AIS" -f "$DI" -v 'hello world' foo bar >/dev/null
"$AIS" -f "$DI" --dump | "$AIS" -f "$DJ" --import >/dev/null 2>&1
ok      "dump|import: value round-trips"       "hello world" "$("$AIS" -f "$DJ" foo)"
ok      "dump|import: key 'bar' preserved"     "hello world" "$("$AIS" -f "$DJ" bar)"
# the id itself must NOT survive as a key (the corruption signature)
okempty "dump|import: id not stored as a key"  "$("$AIS" -f "$DJ" 1)"
rm -rf "$DI" "$DJ"

# (2) A '|' in a key is the store's field delimiter: stored raw it shifts the value.
PK=$(mktemp -d "${TMPDIR:-/tmp}/ais_pk.XXXXXX") || exit 2
"$AIS" -f "$PK" -v PAYDAY 'money|bank' >/dev/null
okeq    "pipe-in-key: value is exactly PAYDAY, not corrupted" \
        "PAYDAY" "$("$AIS" -f "$PK" 'money|bank' | sed 's/^[0-9]*|//')"
rm -rf "$PK"

# (3) A newline in a value is refused: fgets would drop the tail on readback.
NL=$(mktemp -d "${TMPDIR:-/tmp}/ais_nl.XXXXXX") || exit 2
nlout=$("$AIS" -f "$NL" -v "$(printf 'part_A\npart_B')" note 2>&1)
ok      "newline-value: refused with a clear message" "multiple lines" "$nlout"
okempty "newline-value: nothing was stored"           "$("$AIS" -f "$NL" --dump 2>/dev/null)"
rm -rf "$NL"

# --- folder sync must never invent its target -------------------------------
#     A typo, or an unplugged drive whose mount point is an empty directory, must
#     fail loudly rather than be created and reported as a successful backup.
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
#     The "mts" sidecar carries the last local edit; merge and export use the
#     later of it and the record's creation time.
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
#     attached, and what every GUI save path calls; it must stamp the edit too.
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
#     The exported timestamp decides key attaches as well as record deletes.
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
#     An attach carries its own time (T|), so it can outrank a peer's detach.
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
#     A record surviving a delete is restamped, and that stamp also decides key
#     attaches, so it must not re-advertise a tag another device removed.
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
#     D| lines resolve a batch at a time (ais_merge_del_many): 300 deletes exercise
#     the flush at the buffer boundary and at end of stream.
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

# --- invariants the coming redesigns would break silently ----------------------
#     doc/dev/FORMAT_V2.md plans to drop ids from --dump and renumber densely at
#     compaction. The three assumptions that rests on are pinned here.

# INVARIANT 1: the wire carries NO ids -- cross-device identity is content_hash,
#     and an id is meaningful only on the device that minted it.
IV=$(mktemp -d "${TMPDIR:-/tmp}/ais_inv.XXXXXX") || exit 2
"$AIS" -f "$IV" -v 'http://one' alpha >/dev/null
"$AIS" -f "$IV" -v 'http://two' beta  >/dev/null
"$AIS" -f "$IV" --add 1 -v 'http://one-b' >/dev/null 2>&1
id2=$("$AIS" -f "$IV" beta | cut -d'|' -f1); "$AIS" -f "$IV" -y --del "$id2" >/dev/null 2>&1
"$AIS" -f "$IV" --export > "$IV/stream" 2>/dev/null
ok      "wire: A| lines carry keys and value, no id"  "^A|" "$(cat "$IV/stream")"
# every verb that NAMES an existing record must name it by hash, never by number
okeq    "wire: no verb line carries a numeric id field" "0" \
        "$(grep -cE '^[A-Z]\|[0-9]+\|' "$IV/stream" || true)"
ok      "wire: a delete travels as a hash"            "^D|" "$(cat "$IV/stream")"
ok      "wire: an extra link travels as a hash"       "^M|" "$(cat "$IV/stream")"

# INVARIANT 2: a value names ONE record, on every write path. Two records sharing
#     a value make a peer collapse them, and a later delete of either takes both.
IW=$(mktemp -d "${TMPDIR:-/tmp}/ais_inv2.XXXXXX") || exit 2
"$AIS" -f "$IW" -v 'the value' k1 >/dev/null
"$AIS" -f "$IW" -v 'other'     k2 >/dev/null
o2=$("$AIS" -f "$IW" k2 | cut -d'|' -f1)
ok      "identity: --add refuses a value another record holds" "already holds" \
        "$("$AIS" -f "$IW" --add "$o2" -v 'the value' 2>&1)"
ok      "identity: --set refuses it too"                       "unchanged" \
        "$("$AIS" -f "$IW" --set "$o2" -v 'other' -v 'the value' 2>&1)"
okeq    "identity: so the value still names exactly one record" "1" \
        "$("$AIS" -f "$IW" --dump | grep -c 'the value')"
# put's own idempotency is the third path, and it unions keys rather than duplicating
"$AIS" -f "$IW" -v 'the value' k3 >/dev/null
okeq    "identity: re-putting a held value adds no record"      "1" \
        "$("$AIS" -f "$IW" --dump | grep -c 'the value')"
ok      "identity: and unions the new key onto it"             "the value" "$("$AIS" -f "$IW" k3)"

# INVARIANT 3: the index is DISPOSABLE -- idx/ is rebuildable from the store.
IX=$(mktemp -d "${TMPDIR:-/tmp}/ais_inv3.XXXXXX") || exit 2
for w in one two three; do "$AIS" -f "$IX" -v "http://$w" tag "$w" >/dev/null; done
rm -rf "$IX/idx" "$IX/off"
"$AIS" -f "$IX" -y --compact >/dev/null 2>&1
ok      "disposable: recall works after deleting idx/ and compacting" "http://two" \
        "$("$AIS" -f "$IX" two)"
okeq    "disposable: every record came back"          "3" "$("$AIS" -f "$IX" tag | grep -c .)"
ok      "disposable: and the store never needed it"   "http://three" "$("$AIS" -f "$IX" --dump)"
rm -rf "$IV" "$IW" "$IX"

# --- three silent-drop paths in import ------------------------------------------
IS=$(mktemp -d "${TMPDIR:-/tmp}/ais_isd.XXXXXX") || exit 2
# 1. a --dump line with an authoritative EMPTY keys field (--untag leaves these)
printf '5||orphan\n6|k|kept\n' > "$IS/stream"
printf 'y\ny\n' > "$IS/ans"
isout=$(AIS_TTY="$IS/ans" "$AIS" -f "$IS" --import-interactively < "$IS/stream" 2>&1)
ok      "silent-drop: interactive keeps an untagged dump record" "imported 2 of 2" "$isout"
ok      "silent-drop: and the orphan is really there"            "orphan" "$("$AIS" -f "$IS" --dump)"
# 2. a short uppercase key with a date-like value must not read as a merge verb
IS2=$(mktemp -d "${TMPDIR:-/tmp}/ais_isd2.XXXXXX") || exit 2
printf 'AI|2026-01-01 met Ann\nTV|2025-12-31 finale\n' | "$AIS" -f "$IS2" --import >/dev/null 2>&1
ok      "silent-drop: a short key with a dated value survives"   "met Ann"  "$("$AIS" -f "$IS2" AI)"
ok      "silent-drop: and the second one too"                    "finale"   "$("$AIS" -f "$IS2" TV)"
# a genuine merge verb must STILL be refused, or the guard has been disarmed
mv=$(printf 'ZZ|2026-01-01T00:00:00Z|deadbeef\n' | "$AIS" -f "$IS2" --import 2>&1)
ok      "silent-drop: a real verb line is still refused"         "unknown"  "$mv"
# 3. a key beginning '..' must not put its posting above idx/
IS3=$(mktemp -d "${TMPDIR:-/tmp}/ais_isd3.XXXXXX") || exit 2
"$AIS" -f "$IS3" -v payload '../../victim/pwned' >/dev/null 2>&1
okeq    "path-escape: nothing was written outside idx/"          "0" \
        "$(ls "$IS3" | grep -c '\.\.' || true)"
ok      "path-escape: and the record still recalls"              "payload" \
        "$("$AIS" -f "$IS3" -- '../../victim/pwned' 2>/dev/null)"
rm -rf "$IS" "$IS2" "$IS3"

# --- an over-long import line must not fabricate a record from its tail --------
#     fgets returns an over-long line as two, and everything past offset 65535 is
#     chosen by whoever wrote the file. The whole line is refused, once.
OL=$(mktemp -d "${TMPDIR:-/tmp}/ais_ol.XXXXXX") || exit 2
{ printf 'mytag|'; head -c 65529 /dev/zero | tr '\0' 'A'
  printf '9999|2026-01-01T00:00:00Z|forgedkeys|forgedvalue\n'; } > "$OL/long"
olout=$("$AIS" -f "$OL" --import < "$OL/long" 2>&1)
ok      "longline: the whole line is refused, once"  "skipped whole"  "$olout"
ok      "longline: and nothing was imported"         "imported 0"     "$olout"
okempty "longline: no record was fabricated"         "$("$AIS" -f "$OL" forgedkeys 2>/dev/null)"
okempty "longline: the store stayed empty"           "$("$AIS" -f "$OL" --dump 2>/dev/null)"
# --import-interactively shares the reader
printf 'y\ny\n' > "$OL/ans"
oli=$(AIS_TTY="$OL/ans" "$AIS" -f "$OL" --import-interactively < "$OL/long" 2>&1)
okempty "longline: --import-interactively is not fooled either" \
        "$("$AIS" -f "$OL" forgedkeys 2>/dev/null)"
rm -rf "$OL"

# --- --import-interactively: the per-record gate -------------------------------
#     A bare Enter SKIPS (feed.c takes only y/Y), matching the [y/N] prompt: the
#     two defaults differ by "took everything" against "took nothing".
II=$(mktemp -d "${TMPDIR:-/tmp}/ais_ii.XXXXXX") || exit 2
printf 'k1|http://take-me\nk2|http://skip-me\nk3|http://take-me-too\n' > "$II/stream"
printf 'y\nn\ny\n' > "$II/ans"
iiout=$(AIS_TTY="$II/ans" "$AIS" -f "$II" --import-interactively < "$II/stream" 2>&1)
ok      "import-i: reports how many of how many"   "imported 2 of 3" "$iiout"
ok      "import-i: an accepted record is stored"   "http://take-me"  "$("$AIS" -f "$II" k1)"
ok      "import-i: the second accepted one too"    "http://take-me-too" "$("$AIS" -f "$II" k3)"
okempty "import-i: a refused record is NOT stored" "$("$AIS" -f "$II" k2 2>/dev/null)"
ok      "import-i: shows the record before asking" "take into your index" "$iiout"
# bare Enter must skip, matching the [y/N] the prompt shows
II2=$(mktemp -d "${TMPDIR:-/tmp}/ais_ii2.XXXXXX") || exit 2
printf '\n\n\n' > "$II2/ans"
ii2=$(AIS_TTY="$II2/ans" "$AIS" -f "$II2" --import-interactively < "$II/stream" 2>&1)
ok      "import-i: bare Enter skips (the [y/N] default)" "imported 0 of 3" "$ii2"
okempty "import-i: and nothing was stored"         "$("$AIS" -f "$II2" k1 2>/dev/null)"
rm -rf "$II" "$II2"

# --- the help is documentation, so hold it to the code as ground truth -------
#     The check is derived from main.c's optstring, not from a hand-written list,
#     so a flag added later cannot go silently undocumented.
LH=$("$AIS" --help 2>&1)
opts=$(sed -n 's/.*getopt_long(argc, argv, "\([^"]*\)".*/\1/p' \
       "$(dirname "$0")/../c/main.c" 2>/dev/null | head -1)
if [ -z "$opts" ]; then
    fail=$((fail + 1)); echo "  FAIL help: could not read the optstring from c/main.c"
else
    #     A plain grep for "-d" also matches --del/--dump/--doc/--default, so
    #     require a SHORT flag: not preceded by '-', and ending the word.
    miss=""
    for f in $(printf '%s' "$opts" | tr -d ':' | sed 's/./& /g'); do
        printf '%s' "$LH" | grep -qE "(^|[^-])-$f([,[:space:]]|$)" || miss="$miss -$f"
    done
    okeq "help: every short flag in the optstring appears in --help" "" "$miss"
fi

#     Same for the LONG options, read from the getopt_long table. --del-key is the
#     one deliberate omission: a working alias that is not advertised.
MANP="$(dirname "$0")/../man/ais.1"
longs=$(sed -n '/static const struct option longopts/,/{ NULL, 0, NULL, 0 }/p' \
        "$(dirname "$0")/../c/main.c" 2>/dev/null | sed -n 's/.*{ "\([^"]*\)".*/\1/p')
if [ -z "$longs" ]; then
    fail=$((fail + 1)); echo "  FAIL help: could not read the longopts table from c/main.c"
else
    missh=""; missm=""
    for o in $longs; do
        [ "$o" = "del-key" ] && continue
        printf '%s' "$LH" | grep -q -- "--$o" || missh="$missh --$o"
        [ -f "$MANP" ] && { grep -q -- "\\\\-\\\\-$o" "$MANP" || missm="$missm --$o"; }
    done
    okeq "help: every long option in the table appears in --help" "" "$missh"
    okeq "man: every long option in the table appears in ais.1"   "" "$missm"
fi

#     doc/command_line.txt is `ais --help` checked in for the web. Compare from
#     the first help line down, skipping the hand-written header above it.
CLDOC="$(dirname "$0")/../doc/command_line.txt"
if [ ! -f "$CLDOC" ]; then
    fail=$((fail + 1)); echo "  FAIL doc/command_line.txt: missing (run: make helpdoc)"
else
    CLD=$(mktemp -d "${TMPDIR:-/tmp}/ais_cldoc.XXXXXX") || exit 2
    sed -n '/^ais: associative index/,$p' "$CLDOC" > "$CLD/doc"
    printf '%s\n' "$LH" > "$CLD/bin"
    okeq "doc/command_line.txt matches ais --help (run: make helpdoc)" "" \
         "$(diff "$CLD/doc" "$CLD/bin" 2>&1)"
    rm -rf "$CLD"
fi

# The GET row has no --word, so a bare noun there garden-paths as a verb.
ok      "help: the GET row leads with a verb"      "get records under ALL keys" "$LH"
# The short usage says "save VALUE"; the long help must not say "file VALUE".
okeq    "help: save is spelled the same in both helps" "0" \
        "$(printf '%s' "$LH" | grep -c 'file VALUE\|file each stdin')"
# locate.c resolves FOUR levels; the help must name all of them.
ok      "help: index location lists named indexes"  "ais --switch NAME" "$LH"
ok      "help: and marks the old default legacy"    "legacy single default" "$LH"
# --del/--set/--add/--update all take an ID; the help must say where one comes from.
ok      "help: says where an ID comes from"         "id|value" "$LH"
# and that claim must stay true: get is what prints the handle
GH=$(mktemp -d "${TMPDIR:-/tmp}/ais_gh.XXXXXX") || exit 2
"$AIS" -f "$GH" -v http://handle hk >/dev/null 2>&1
ok      "get: still prints the id|value handle"     "^[0-9][0-9]*|http://handle" \
        "$("$AIS" -f "$GH" hk 2>/dev/null)"
ok      "--find: prints it too"                     "^[0-9][0-9]*|http://handle" \
        "$("$AIS" -f "$GH" --find handle 2>/dev/null)"
okeq    "--dump: and still carries no id"           "0" \
        "$("$AIS" -f "$GH" --dump 2>/dev/null | grep -c '^[0-9][0-9]*|')"
rm -rf "$GH"

# -e -v -: a piped secret longer than the 1023-byte value buffer is refused, not
#      truncated. The guard fires on the read, before any passphrase prompt, so it
#      needs neither a tty nor the crypto module; at exactly 1023 bytes plus the
#      pipe's newline it must NOT fire.
EE=$(mktemp -d "${TMPDIR:-/tmp}/ais_elong.XXXXXX") || exit 2
elong=$(awk 'BEGIN{for(i=0;i<1024;i++)printf "a"}' \
        | AIS_TTY=/nonexistent/path "$AIS" -f "$EE" sk -v - -e 2>&1); erc=$?
ok      "-e -v -: an overlong piped secret is refused" "secret longer than 1023" "$elong"
okeq    "-e -v -: and exits nonzero"          "nz" "$([ "$erc" -ne 0 ] && echo nz)"
okempty "-e -v -: nothing was stored"         "$("$AIS" -f "$EE" --dump 2>/dev/null)"
eedge=$(awk 'BEGIN{for(i=0;i<1023;i++)printf "a"; printf "\n"}' \
        | AIS_TTY=/nonexistent/path "$AIS" -f "$EE" sk -v - -e 2>&1)
okeq    "-e -v -: 1023 bytes + pipe newline pass the guard" "0" \
        "$(printf '%s' "$eedge" | grep -c 'secret longer')"
rm -rf "$EE"


# Foreign importers: a browser's bookmarks HTML and a Takeout Keep folder.
# Keys are only names the user made (folder/label words, engine-normalized) plus
# one constant marker key, so `--del-under bookmark` reverses the whole batch.
FIX="$(dirname "$0")/fixtures"
FI=$(mktemp -d "${TMPDIR:-/tmp}/ais_fimp.XXXXXX") || exit 2
bmout=$("$AIS" -f "$FI" --import-bookmarks "$FIX/bookmarks.html" 2>&1); bmrc=$?
ok      "bookmarks: 4 imported, the malformed <A> skipped" "imported 4, skipped 1" "$bmout"
okeq    "bookmarks: exit 0 when something imported"        "0" "$bmrc"
bmdump=$("$AIS" -f "$FI" --dump)
ok      "bookmarks: root entry gets just 'bookmark'"       "^bookmark -v http://root.example/ Root Link" "$bmdump"
ok      "bookmarks: folder words + marker key"             "^old recipes bookmark -v http://pie" "$bmdump"
ok      "bookmarks: nested folder inherits the path"       "^old recipes baking bookmark -v http://bread.example/ Bread" "$bmdump"
ok      "bookmarks: entities decode in URL and title"      "?a=1&b=2 Grandma's Pie & Cake" "$bmdump"
ok      "bookmarks: untitled entry is the URL alone"       "^bookmark -v http://untitled.example/$" "$bmdump"
ok      "bookmarks: recall by a folder word"               "bread" "$("$AIS" -f "$FI" baking)"
"$AIS" -f "$FI" --del-under bookmark -y >/dev/null 2>&1
okempty "bookmarks: --del-under bookmark reverses it all"  "$("$AIS" -f "$FI" bookmark 2>/dev/null)"
printf '<p>no anchors here</p>\n' > "$FI.none.html"
"$AIS" -f "$FI" --import-bookmarks "$FI.none.html" 2>/dev/null; nbrc=$?
okeq    "bookmarks: nothing imported exits 1"              "1" "$nbrc"
rm -f "$FI.none.html"
rm -rf "$FI"

KI=$(mktemp -d "${TMPDIR:-/tmp}/ais_keep.XXXXXX") || exit 2
kout=$("$AIS" -f "$KI" --import-keep "$FIX/keep" 2>&1); krc=$?
ok      "keep: 3 imported, the trashed note skipped"       "imported 3, skipped 1" "$kout"
okeq    "keep: exit 0 when something imported"             "0" "$krc"
kdump=$("$AIS" -f "$KI" --dump)
ok      "keep: label words + marker, single line inline"   "^cooking ideas keep -v one line note" "$kdump"
ok      "keep: multi-line note went out of line"           "^errands keep -v blobs/" "$kdump"
ok      "keep: the document recalls whole"                 "second line" "$("$AIS" -f "$KI" errands)"
ok      "keep: title leads the document"                   "Shopping" "$("$AIS" -f "$KI" errands)"
ok      "keep: list items become '- ' lines"               "- bread \[rye\]" "$("$AIS" -f "$KI" keep)"
okempty "keep: the trashed note is not here"               "$("$AIS" -f "$KI" --find trash 2>/dev/null)"
: > "$KI.zip"
zout=$("$AIS" -f "$KI" --import-keep "$KI.zip" 2>&1); zrc=$?
ok      "keep: a .zip is refused with the extract hint"    "extract the .zip" "$zout"
okeq    "keep: and exits nonzero"                          "nz" "$([ "$zrc" -ne 0 ] && echo nz)"
rm -f "$KI.zip"

# The port check --serve already has, now on both LAN-serve forms too.
pout=$("$AIS" -f "$KI" --export --serve 99999 2>&1); prc=$?
ok      "port: --export --serve 99999 says the range"      "1..65535" "$pout"
okeq    "port: --export --serve 99999 exits 2"             "2" "$prc"
pout=$("$AIS" -f "$KI" --sync --serve 0 2>&1); prc=$?
ok      "port: --sync --serve 0 says the range"            "1..65535" "$pout"
okeq    "port: --sync --serve 0 exits 2"                   "2" "$prc"
rm -rf "$KI"

# ---- --mcp: the agent tool server -----------------------------------------
# It is a pipe, so the test is a pipe: feed whole JSON-RPC lines in, read the
# replies out. One session per case keeps a failure from cascading into the next.
MI=$(mktemp -d "${TMPDIR:-/tmp}/ais_mcp.XXXXXX") || exit 2
"$AIS" -f "$MI" -v http://example.org/venice venice italy >/dev/null
"$AIS" -f "$MI" -v /photos/IMG_1.jpg venice photo >/dev/null

# mcp REQUEST...  -- run one session, echo what came back
mcp() { printf '%s\n' "$@" | "$AIS" -f "$MI" --mcp 2>/dev/null; }
mcprw() { printf '%s\n' "$@" | "$AIS" -f "$MI" --mcp rw 2>/dev/null; }

# jsonok LABEL TEXT -- pass if EVERY line of TEXT parses as JSON. Substring
# greps cannot see a stray quote inside a description, which is exactly how an
# unparseable tools/list once passed 30 green assertions.
if command -v python3 >/dev/null 2>&1; then HAVE_PY=1; else HAVE_PY=0; fi
jsonok() {
    if [ "$HAVE_PY" -ne 1 ]; then echo "  skip $1 (no python3)"; return; fi
    if printf '%s\n' "$2" | python3 -c '
import sys, json
for line in sys.stdin:
    if line.strip():
        json.loads(line)
' 2>/dev/null; then
        pass=$((pass + 1)); echo "  ok   $1"
    else
        fail=$((fail + 1)); echo "  FAIL $1 -- not valid JSON: [$2]"
    fi
}

mout=$(mcp '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}')
jsonok  "mcp: initialize is valid JSON"                    "$mout"
ok      "mcp: initialize names the server"                 '"name":"ais"' "$mout"
ok      "mcp: and says which index it opened"              "$MI" "$mout"
ok      "mcp: read-only says saving is off"                'read-only and has no save tool' "$mout"
ok      "mcp: and answers the protocol asked for"          '"protocolVersion":"2025-06-18"' "$mout"
ok      "mcp: and tells it how to turn saving on"          "restarting it as 'ais --mcp rw'" "$mout"
ok      "mcp: and not to keep the note itself"             'Do not offer to remember it yourself' "$mout"
okempty "mcp: read-only never says call save"              "$(printf '%s' "$mout" | grep -o 'call save')"
mout=$(mcprw '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}')
ok      "mcp: rw carries the save instructions"            'call save' "$mout"
ok      "mcp: which tell it to ask for the keys"           'ASK which keys' "$mout"
okempty "mcp: and rw never says there is no save tool"     "$(printf '%s' "$mout" | grep -o 'no save tool')"
ok      "mcp: a first miss is not the end of the search"   'retry with match any' "$mout"
ok      "mcp: tags is where the user's own word is"        'a singular or a short form' "$mout"
ok      "mcp: timeline is asked for a small count"         'call timeline with a small count' "$mout"
ok      "mcp: and is not mistaken for a date filter"       'no date filter' "$mout"
ok      "mcp: match all is explained where it is read"     'filed under BOTH' "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"nonsense"}}')
ok      "mcp: a nonsense version gets ours, not an echo"   '"protocolVersion":"20' "$mout"

mout=$(mcp '{"jsonrpc":"2.0","id":2,"method":"tools/list"}')
jsonok  "mcp: tools/list is valid JSON"                    "$mout"
ok      "mcp: tools/list offers recall"                    '"name":"recall"' "$mout"
ok      "mcp: tools/list offers tags"                      '"name":"tags"' "$mout"
okempty "mcp: read-only hides save"                        "$(printf '%s' "$mout" | grep -o '"name":"save"')"
mout=$(mcprw '{"jsonrpc":"2.0","id":2,"method":"tools/list"}')
ok      "mcp: rw offers save"                              '"name":"save"' "$mout"

jsonok  "mcp: tools/list with save is valid JSON"          "$mout"
okempty "mcp: the match-all sentence is not said twice"    "$(printf '%s' "$mout" | grep -o 'filed under BOTH')"
ok      "mcp: save says where a multi-line value goes"     'written to a file inside the index' "$mout"
ok      "mcp: tags says the default is only the busiest"   'raise limit before concluding a key is absent' "$mout"
ok      "mcp: timeline declares limit as well as count"    '"limit":{"type":"integer","description":"the same as count' "$mout"

mout=$(mcp '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"recall","arguments":{"keys":["venice","italy"]}}}')
ok      "mcp: recall intersects the keys"                  "example.org/venice" "$mout"
okempty "mcp: and leaves the other record out"             "$(printf '%s' "$mout" | grep -o 'IMG_1')"
mout=$(mcp '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"recall","arguments":{"keys":"venice italy","match":"any"}}}')
ok      "mcp: match any unions them"                       "IMG_1" "$mout"
ok      "mcp: keys as one string split on blanks"          "example.org" "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"recall","arguments":{"keys":["venice"],"limit":1}}}')
ok      "mcp: limit 1 returns the first match"             "example.org" "$mout"
okempty "mcp: and stops before the second"                 "$(printf '%s' "$mout" | grep -o 'IMG_1')"
mout=$(mcp '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"recall","arguments":{"keys":["nothing"]}}}')
ok      "mcp: an empty result says so in words"            "no match" "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"recall","arguments":{}}}')
ok      "mcp: recall with no key is a tool error"          '"isError":true' "$mout"

mout=$(mcp '{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"find","arguments":{"text":"EXAMPLE"}}}')
ok      "mcp: find matches regardless of case"             "example.org" "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"tags","arguments":{}}}')
ok      "mcp: tags lists the vocabulary, busiest first"    "2|venice" "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"timeline","arguments":{"count":1}}}')
ok      "mcp: timeline carries keys and a timestamp"       "|venice photo|" "$mout"

# The write bit is the whole security story: it has to be handed over on purpose.
mout=$(mcp '{"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"save","arguments":{"value":"x","keys":["k"]}}}')
ok      "mcp: read-only refuses save, readably"            "read-only" "$mout"
ok      "mcp: and marks it as a tool error"                '"isError":true' "$mout"
okempty "mcp: nothing was written"                         "$("$AIS" -f "$MI" k 2>/dev/null)"
mout=$(mcprw '{"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"save","arguments":{"value":"written by the agent","keys":["k"]}}}')
ok      "mcp: rw saves and names the record"               "saved as record" "$mout"
ok      "mcp: and the CLI sees it"                         "written by the agent" "$("$AIS" -f "$MI" k)"

# JSON the server must survive: escapes, a bad message, an unknown method.
mout=$(mcprw '{"jsonrpc":"2.0","id":13,"method":"tools/call","params":{"name":"save","arguments":{"value":"caf\u00e9 \"quoted\" \ud83d\ude00","keys":["esc"]}}}')
ok      "mcp: \\u escapes and a surrogate pair decode"      "saved as record" "$mout"
ok      "mcp: the stored value round-trips"                "café" "$("$AIS" -f "$MI" esc)"
mout=$(mcp '{"jsonrpc":"2.0","id":14,"method":"tools/call","params":{"name":"recall","arguments":{"keys":["esc"]}}}')
ok      "mcp: a quote comes back escaped, not raw"         '\\"quoted\\"' "$mout"
mout=$(mcp 'not json at all')
ok      "mcp: a malformed line is a parse error"           '"code":-32700' "$mout"
ok      "mcp: with a null id, as JSON-RPC requires"        '"id":null' "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":15,"method":"nosuch"}')
ok      "mcp: an unknown method is -32601"                 '"code":-32601' "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":16,"method":"tools/call","params":{"name":"bogus","arguments":{}}}')
ok      "mcp: an unknown tool is -32602"                   '"code":-32602' "$mout"
okempty "mcp: a notification draws no reply"               "$(mcp '{"jsonrpc":"2.0","method":"notifications/initialized"}')"
mout=$(mcp "$(printf '{"jsonrpc":"2.0","id":17,"method":"tools/call","params":{"name":"find","arguments":{"text":"%s"}}}' "$(awk 'BEGIN{while(i++<70000)printf "x"}')")")
ok      "mcp: an oversized request is refused, not split"  '"code":-32600' "$mout"

mout=$(mcprw \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}' \
  '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"recall","arguments":{"keys":["venice"]}}}' \
  '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"tags","arguments":{}}}' \
  '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"timeline","arguments":{"count":2}}}' \
  '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"find","arguments":{"text":"photos"}}}')
jsonok  "mcp: a whole rw session is valid JSON throughout" "$mout"
okeq    "mcp: six requests, six replies"                   "6" "$(printf '%s\n' "$mout" | grep -c '^{')"
okempty "mcp: stderr stays empty on the good paths"        "$(printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' | "$AIS" -f "$MI" --mcp 2>&1 >/dev/null)"

# The id goes back as the bytes that came in: a client that cannot match it
# drops the reply and waits forever.
mout=$(mcp '{"jsonrpc":"2.0","id":"a\"b","method":"ping"}')
jsonok  "mcp: a string id with a quote survives"           "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":12345678901234567890,"method":"ping"}')
ok      "mcp: an id past LONG_MAX is echoed, not clamped"  '"id":12345678901234567890' "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":1.5,"method":"ping"}')
ok      "mcp: a fractional id is echoed, not truncated"    '"id":1.5' "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":null,"method":"ping"}')
ok      "mcp: id null is a request, and is answered"       '"id":null' "$mout"

# A NUL cannot be carried by a C string, so the line has to be refused whole:
# reading it with fgets used to drop that request AND the one after it.
mout=$(printf '\000{"jsonrpc":"2.0","id":7,"method":"ping"}\n{"jsonrpc":"2.0","id":8,"method":"ping"}\n' | "$AIS" -f "$MI" --mcp 2>/dev/null)
ok      "mcp: a leading NUL is refused, not swallowed"     '"code":-32600' "$mout"
ok      "mcp: and the next request still answers"          '"id":8' "$mout"
mout=$(printf '{"jsonrpc":"2.0","id":7,\000"method":"ping"}\n{"jsonrpc":"2.0","id":8,"method":"ping"}\n' | "$AIS" -f "$MI" --mcp 2>/dev/null)
ok      "mcp: a NUL mid-line does not eat the next line"   '"id":8' "$mout"

# The store takes any byte the CLI is given; JSON must be UTF-8, and one bad
# byte would cost the whole reply rather than one row.
"$AIS" -f "$MI" -v "$(printf 'raw \377 byte')" badutf >/dev/null 2>&1
mout=$(mcp '{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"recall","arguments":{"keys":["badutf"]}}}')
jsonok  "mcp: an invalid UTF-8 value still decodes"        "$mout"
ok      "mcp: the bad byte became a replacement char"      'ufffd' "$mout"

# A record under no key cannot be recalled by any key, ever.
mout=$(mcprw '{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"save","arguments":{"value":"orphan"}}}')
ok      "mcp: a keyless save is refused"                   "needs at least one key" "$mout"
ok      "mcp: and the refusal teaches the ask"             "separated by spaces" "$mout"
okempty "mcp: nothing was stored"                          "$("$AIS" -f "$MI" --find orphan 2>/dev/null)"
mout=$(mcprw '{"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"save","arguments":{"value":"tunnel cmd","keys":["work","ssh"]}}}')
ok      "mcp: the save reply names the keys back"          "under work ssh" "$mout"

# A page that is not marked as a page gets reported as the whole index.
mout=$(mcp '{"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"recall","arguments":{"keys":["venice"],"limit":1}}}')
ok      "mcp: a spent budget says there may be more"       "stopped at the limit" "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":13,"method":"tools/call","params":{"name":"recall","arguments":{"keys":["venice"],"limit":4294967296}}}')
okempty "mcp: a limit past INT_MAX is not unbounded"       "$(printf '%s' "$mout" | grep -o 'stopped at the limit')"

# A page that is marked one row too early reads as a page that is not there.
mout=$(mcp '{"jsonrpc":"2.0","id":20,"method":"tools/call","params":{"name":"recall","arguments":{"keys":["venice"],"limit":2}}}')
okempty "mcp: exactly limit rows is not a page"            "$(printf '%s' "$mout" | grep -o 'stopped at the limit')"
ok      "mcp: and both rows are there"                     "IMG_1" "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":21,"method":"tools/call","params":{"name":"tags","arguments":{"limit":2}}}')
ok      "mcp: tags says when it stopped at the limit"      "stopped at the limit" "$mout"
okeq    "mcp: and emits exactly the limit"                 "2" "$(printf '%s' "$mout" | grep -o '|' | wc -l | tr -d ' ')"
mout=$(mcp '{"jsonrpc":"2.0","id":22,"method":"tools/call","params":{"name":"find","arguments":{"text":"e","limit":1}}}')
ok      "mcp: find marks a spent budget as well"           "stopped at the limit" "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":22,"method":"tools/call","params":{"name":"find","arguments":{"text":"example.org/venice"}}}')
okempty "mcp: and a single match is not marked as a page"  "$(printf '%s' "$mout" | grep -o 'stopped at the limit')"

# A wrong match, or an argument no tool has, must not read as a filter applied.
mout=$(mcp '{"jsonrpc":"2.0","id":23,"method":"tools/call","params":{"name":"recall","arguments":{"keys":["venice"],"match":"some"}}}')
ok      "mcp: match some is refused, not read as all"      '"isError":true' "$mout"
ok      "mcp: and the refusal names the two choices"       "match some is neither all" "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":24,"method":"tools/call","params":{"name":"timeline","arguments":{"since":"2026-09-08"}}}')
ok      "mcp: an argument no tool has is refused"          "timeline has no argument since" "$mout"
ok      "mcp: and it lists what timeline does take"        "it takes count limit" "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":25,"method":"tools/call","params":{"name":"timeline","arguments":{"limit":2}}}')
okempty "mcp: timeline still takes limit as count"         "$(printf '%s' "$mout" | grep -o 'isError')"

# A limit is a whole number in range or it is a refusal: nothing is clamped.
for bad in '1e3' '"10"' '0' '-1' '1001' '1.5'; do
    mout=$(mcp "{\"jsonrpc\":\"2.0\",\"id\":26,\"method\":\"tools/call\",\"params\":{\"name\":\"recall\",\"arguments\":{\"keys\":[\"venice\"],\"limit\":$bad}}}")
    ok  "mcp: limit $bad is refused, not rewritten"        "1 to 1000" "$mout"
done

# A key the store would fold is a key the reply must not promise.
mout=$(mcp '{"jsonrpc":"2.0","id":27,"method":"tools/call","params":{"name":"recall","arguments":{"keys":["a|b"]}}}')
ok      "mcp: a key holding a pipe is refused"             "a key cannot hold" "$mout"
mkeys=$(awk 'BEGIN{for(i=1;i<65;i++)printf "\"k%d\",",i; printf "\"k65\""}')
mout=$(mcp "{\"jsonrpc\":\"2.0\",\"id\":28,\"method\":\"tools/call\",\"params\":{\"name\":\"recall\",\"arguments\":{\"keys\":[$mkeys]}}}")
ok      "mcp: past 64 keys is refused, not truncated"      "at most 64 keys" "$mout"

# What save tells the model back has to be what happened.
mout=$(mcprw '{"jsonrpc":"2.0","id":29,"method":"tools/call","params":{"name":"save","arguments":{"value":"written by the agent","keys":["k2"]}}}')
ok      "mcp: a value already held gains keys, not a record" "to existing record" "$mout"
ok      "mcp: and the reply names every key it now has"    "now under k k2" "$mout"
mout=$(mcprw '{"jsonrpc":"2.0","id":30,"method":"tools/call","params":{"name":"save","arguments":{"value":"first line\nsecond line","keys":["doc"]}}}')
ok      "mcp: a multi-line save says it made a document"   "as document blobs/" "$mout"
ok      "mcp: and warns that find cannot read inside it"   "find does not search inside documents" "$mout"
mout=$(mcprw '{"jsonrpc":"2.0","id":31,"method":"tools/call","params":{"name":"save","arguments":{"value":"\r","keys":["cr"]}}}')
ok      "mcp: a blank-space value says why it failed"      "blank space only" "$mout"
ok      "mcp: and it is a tool error"                      '"isError":true' "$mout"
mout=$(mcprw '{"jsonrpc":"2.0","id":32,"method":"tools/call","params":{"name":"save","arguments":{"value":"aisc:AAAA","keys":["plant"]}}}')
ok      "mcp: a planted secret marker is refused"          "reserved prefix" "$mout"
mout=$(mcprw '{"jsonrpc":"2.0","id":33,"method":"tools/call","params":{"name":"save","arguments":{"value":"blobs/x.txt","keys":["plant"]}}}')
ok      "mcp: a planted document path is refused too"      "reserved prefix" "$mout"
okempty "mcp: and neither was stored"                      "$("$AIS" -f "$MI" plant 2>/dev/null)"

# Ciphertext never reaches a model, and a document path that leaves the index
# is withheld rather than handed over.
"$AIS" -f "$MI" -v 'blobs/../../etc/passwd' escaped >/dev/null
"$AIS" -f "$MI" -v 'aisc:c2VjcmV0' sec >/dev/null
mout=$(mcp '{"jsonrpc":"2.0","id":34,"method":"tools/call","params":{"name":"recall","arguments":{"keys":["escaped","sec"],"match":"any"}}}')
ok      "mcp: a blobs path that escapes is withheld"       "document path withheld" "$mout"
okempty "mcp: the path itself is not in the reply"         "$(printf '%s' "$mout" | grep -o 'etc/passwd')"
ok      "mcp: a secret reads as encrypted and hidden"      "encrypted, hidden" "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":35,"method":"tools/call","params":{"name":"find","arguments":{"text":"c2VjcmV0"}}}')
ok      "mcp: find still matches the stored ciphertext"    "encrypted, hidden" "$mout"
okempty "mcp: but hands none of it back"                   "$(printf '%s' "$mout" | grep -o 'c2VjcmV0')"
mout=$(mcp '{"jsonrpc":"2.0","id":36,"method":"tools/call","params":{"name":"timeline","arguments":{"count":2}}}')
ok      "mcp: timeline hides it too"                       "encrypted, hidden" "$mout"

# An empty index is not a missed match, and saying "no match" sends a model
# hunting for a key that could not exist.
MI2=$(mktemp -d "${TMPDIR:-/tmp}/ais_mcp.XXXXXX") || exit 2
mout=$(printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"timeline","arguments":{}}}' | "$AIS" -f "$MI2" --mcp 2>/dev/null)
ok      "mcp: an empty timeline says the index is empty"   "the index is empty" "$mout"
rm -rf "$MI2"

# A client that gets no reply waits forever, so every fault has to answer.
mout=$(mcp '{"jsonrpc":"2.0","id":{"a":1},"method":"ping"}')
ok      "mcp: an object id is refused, not ignored"        '"code":-32600' "$mout"
ok      "mcp: and the refusal carries id null"             '"id":null' "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":[1],"method":"ping"}')
ok      "mcp: an array id is refused too"                  '"code":-32600' "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":true,"method":"ping"}')
ok      "mcp: a boolean id is refused too"                 '"code":-32600' "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":+5,"method":"ping"}')
ok      "mcp: id +5 is not a JSON number"                  '"code":-32700' "$mout"
jsonok  "mcp: and the refusal of it still parses"          "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":007,"method":"ping"}')
ok      "mcp: id 007 is not a JSON number either"          '"code":-32700' "$mout"
jsonok  "mcp: and that refusal parses as well"             "$mout"
mout=$(mcp '[{"jsonrpc":"2.0","id":1,"method":"ping"}]')
ok      "mcp: a batch is well-formed JSON, so -32600"      '"code":-32600' "$mout"
ok      "mcp: and it says one object per line"             'no batch' "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":1,"method":"ping"} junk')
ok      "mcp: bytes after the root object are -32700"      '"code":-32700' "$mout"
mout=$(mcp '{"id":1,"method":"ping"}')
ok      "mcp: a missing jsonrpc is -32600"                 '"code":-32600' "$mout"
ok      "mcp: and the id still comes back"                 '"id":1' "$mout"
mout=$(mcp '{"jsonrpc":"1.0","id":1,"method":"ping"}')
ok      "mcp: jsonrpc 1.0 is -32600"                       '"code":-32600' "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"9999-99-99"}}')
ok      "mcp: an invented date version is not agreed to"   '"protocolVersion":"2025-06-18"' "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05"}}')
ok      "mcp: a published revision is answered as asked"   '"protocolVersion":"2024-11-05"' "$mout"

# A key the reply names is a key the store kept, or the save is refused. The
# engine folds '/', '\', '|', space, control bytes and a leading '.' to '_', and
# reads a leading '-' as a detach that files nothing.
mout=$(mcprw '{"jsonrpc":"2.0","id":40,"method":"tools/call","params":{"name":"save","arguments":{"value":"dash value","keys":["-foo"]}}}')
ok      "mcp: a key starting with - is refused"            "cannot begin with '-'" "$mout"
ok      "mcp: and it is a tool error"                      '"isError":true' "$mout"
okempty "mcp: and nothing was stored under it"             "$("$AIS" -f "$MI" --find 'dash value' 2>/dev/null)"
mout=$(mcprw '{"jsonrpc":"2.0","id":41,"method":"tools/call","params":{"name":"save","arguments":{"value":"slash value","keys":["a/b"]}}}')
ok      "mcp: a key holding a slash is refused"            "would file that key under another name" "$mout"
mout=$(mcprw '{"jsonrpc":"2.0","id":42,"method":"tools/call","params":{"name":"save","arguments":{"value":"dot value","keys":[".hidden"]}}}')
ok      "mcp: a key starting with a dot is refused"        "would file that key under another name" "$mout"
mout=$(mcprw '{"jsonrpc":"2.0","id":43,"method":"tools/call","params":{"name":"save","arguments":{"value":"del value","keys":["ab"]}}}')
ok      "mcp: a key holding 0x7F is refused"               "would file that key under another name" "$mout"
mout=$(mcp '{"jsonrpc":"2.0","id":44,"method":"tools/call","params":{"name":"tags","arguments":{"limit":1000}}}')
okempty "mcp: and the index never spelled one a_b"         "$(printf '%s' "$mout" | grep -o 'a_b')"
okempty "mcp: nor turned the leading dot into _hidden"     "$(printf '%s' "$mout" | grep -o '_hidden')"

# A tab inside an array element is a byte the store folds, so it is refused; the
# one-string form is documented as blank-separated and still splits on one.
mout=$(mcprw '{"jsonrpc":"2.0","id":45,"method":"tools/call","params":{"name":"save","arguments":{"value":"tab in element","keys":["a\tb"]}}}')
ok      "mcp: a tab inside an array element is refused"    "would file that key under another name" "$mout"
mout=$(mcprw '{"jsonrpc":"2.0","id":46,"method":"tools/call","params":{"name":"save","arguments":{"value":"tab in string","keys":"tabone\ttabtwo"}}}')
ok      "mcp: a tab in the one-string form still splits"   "under tabone tabtwo" "$mout"

# A put that adds no key added no key.
mout=$(mcprw '{"jsonrpc":"2.0","id":47,"method":"tools/call","params":{"name":"save","arguments":{"value":"held value","keys":["held"]}}}')
ok      "mcp: a value the index lacks is a new record"     "saved as record" "$mout"
mout=$(mcprw '{"jsonrpc":"2.0","id":48,"method":"tools/call","params":{"name":"save","arguments":{"value":"held value","keys":["held"]}}}')
ok      "mcp: saving it again under the same key adds none" "nothing added" "$mout"
mout=$(mcprw '{"jsonrpc":"2.0","id":49,"method":"tools/call","params":{"name":"save","arguments":{"value":"held value","keys":["held","kept"]}}}')
ok      "mcp: one new key among them is still an add"      "to existing record" "$mout"
mout=$(mcprw '{"jsonrpc":"2.0","id":50,"method":"tools/call","params":{"name":"save","arguments":{"value":"held value","keys":["held"]}}}')
ok      "mcp: a subset of the keys it has adds nothing"    "nothing added" "$mout"
ok      "mcp: and the reply still names every key it has"  "under held kept" "$mout"

# A message this reader cannot hold is not a message the client got wrong.
mdeep=$(awk 'BEGIN{for(i=0;i<300;i++)printf "[";for(i=0;i<300;i++)printf "]"}')
mout=$(mcp "{\"jsonrpc\":\"2.0\",\"id\":51,\"method\":\"tools/call\",\"params\":{\"name\":\"recall\",\"arguments\":{\"keys\":$mdeep}}}")
ok      "mcp: a request past the node table is -32600"     '"code":-32600' "$mout"
ok      "mcp: and says how many values it can hold"        "more than 256 values" "$mout"

# JSON-RPC forbids answering a response: two servers answering each other's
# errors never stop.
okempty "mcp: a client's result draws no reply"            "$(mcp '{"jsonrpc":"2.0","id":99,"result":{}}')"
okempty "mcp: nor does a client's error"                   "$(mcp '{"jsonrpc":"2.0","id":98,"error":{"code":-1,"message":"x"}}')"

# The client resolves the index path from ITS directory, not the server's.
MR=$(mktemp -d "${TMPDIR:-/tmp}/ais_mcp.XXXXXX") || exit 2
"$AIS" -f "$MR/rel" --init >/dev/null
mout=$(cd "$MR" && printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
       | "$AIS" -f ./rel --mcp 2>/dev/null)
ok      "mcp: a relative -f is reported absolute"          "This index is at /" "$mout"
okempty "mcp: not as the dot path the server was given"    "$(printf '%s' "$mout" | grep -o 'is at ./rel')"
rm -rf "$MR"

# The caps and the shape of keys belong in the schema, not only in refusals.
mout=$(mcprw '{"jsonrpc":"2.0","id":52,"method":"tools/list"}')
jsonok  "mcp: tools/list still parses with them"           "$mout"
ok      "mcp: the keys schema says one element is one key" "one element is one key" "$mout"
ok      "mcp: and that a single string is taken too"       "a single string with blanks" "$mout"
ok      "mcp: and states the 64-key cap"                   "at most 64" "$mout"
ok      "mcp: and save states the 255-byte key cap"        "a key is at most 255 bytes" "$mout"

mrc=0
"$AIS" -f "$MI" --mcp bogus >/dev/null 2>&1 || mrc=$?
okeq    "mcp: an operand other than rw is a usage error"   "2" "$mrc"
rm -rf "$MI"

# ---- a stored value must never become a path outside the index -------------
# The value is attacker data: it arrives by --import, from a peer's export
# stream, and from the MCP save tool. "aisc:@../victim.txt" made --del zero-fill
# and unlink a file beside the index, because the secret module joined whatever
# followed "aisc:@" onto the index dir.
BI=$(mktemp -d "${TMPDIR:-/tmp}/ais_blobsec.XXXXXX") || exit 2
mkdir "$BI/X"
printf 'SECRET DATA\n' > "$BI/victim.txt"
"$AIS" -f "$BI/X" --init >/dev/null
"$AIS" -f "$BI/X" -v 'aisc:@../victim.txt' k >/dev/null
"$AIS" -f "$BI/X" --del 1 -y >/dev/null 2>&1
ok      "blobsec: --del of an escaping value spares the file"  "SECRET DATA" "$(cat "$BI/victim.txt" 2>&1)"

printf 'SECRET DATA\n' > "$BI/victim2.txt"
printf 'k -v aisc:@../victim2.txt\n' | "$AIS" -f "$BI/X" --import >/dev/null 2>&1
"$AIS" -f "$BI/X" --del 2 -y >/dev/null 2>&1
ok      "blobsec: the same value through --import is as harmless" "SECRET DATA" "$(cat "$BI/victim2.txt" 2>&1)"

# A blob arriving in a merge stream (B|relpath|size + raw bytes) carries the
# peer's chosen name, so the name is attacker data too.
printf 'B|blobs/../evil|5\nEVIL!\nk -v blobs/../evil\n' | "$AIS" -f "$BI/X" --import >/dev/null 2>&1
okempty "blobsec: a B| relpath climbing out of blobs/ writes nothing" "$(ls "$BI/evil" 2>/dev/null)"
okempty "blobsec: and nothing lands in the index dir either"          "$(ls "$BI/X/evil" 2>/dev/null)"

# The legitimate path is untouched: a document still round-trips.
printf 'line one\nline two\n' | "$AIS" -f "$BI/X" --doc note >/dev/null
bdoc=$("$AIS" -f "$BI/X" --dump | sed -n 's/.* -v \(blobs\/.*\)$/\1/p' | tail -1)
ok      "blobsec: --doc still stores a blobs/ value"          "blobs/" "$bdoc"
ok      "blobsec: and the body is readable back"              "line two" "$(cat "$BI/X/$bdoc" 2>&1)"
rm -rf "$BI"

# ---- --mcp will not serve an index it merely found -------------------------
# Step 2 of the index precedence is the nearest .ais walking up, so a cloned
# repository that ships one would become the agent's memory and hand its record
# values to the model. Naming the index with -f is the permission.
PJ=$(mktemp -d "${TMPDIR:-/tmp}/ais_mcpproj.XXXXXX") || exit 2
mkdir -p "$PJ/sub"
"$AIS" -f "$PJ/.ais" --init >/dev/null
"$AIS" -f "$PJ/.ais" -v http://example.org/private cloned >/dev/null
MLIST='{"jsonrpc":"2.0","id":2,"method":"tools/list"}'

# ntools TEXT -- how many tools a tools/list reply offers
ntools() { printf '%s' "$1" | grep -o '"name":"[a-z]*"' | grep -c .; }

pout=$(cd "$PJ/sub" && printf '%s\n' "$MLIST" | "$AIS" --mcp 2>/dev/null); prc=$?
okeq    "mcpproj: a walked-up index exits 2"               "2" "$prc"
okempty "mcpproj: and nothing reaches stdout"              "$pout"
perr=$(cd "$PJ/sub" && printf '%s\n' "$MLIST" | "$AIS" --mcp 2>&1 >/dev/null)
ok      "mcpproj: the refusal names -f"                    "pass -f DIR" "$perr"
ok      "mcpproj: and names the index it declined"         "$PJ/.ais" "$perr"

# Naming the same index serves it, at either setting.
mout=$(printf '%s\n' "$MLIST" | "$AIS" -f "$PJ/.ais" --mcp 2>/dev/null)
jsonok  "mcpproj: the named session is valid JSON"         "$mout"
okeq    "mcpproj: -f serves the four read tools"           "4" "$(ntools "$mout")"
okeq    "mcpproj: -f rw adds save"                         "5" \
        "$(ntools "$(printf '%s\n' "$MLIST" | "$AIS" -f "$PJ/.ais" --mcp rw 2>/dev/null)")"

"$AIS" -f "$PJ/.ais" --mcp rw extra >/dev/null 2>&1
okeq    "mcpproj: a second --mcp operand is a usage error" "2" "$?"
rm -rf "$PJ"

# ---- a key is a filename, so 255 bytes is the wall ------------------------
# Past it the posting file could not be opened, yet the store line and next_id
# were already committed: the record lived in the store, in --timeline and in
# --export, under no tag, and every later --compact failed on the same line.
KL=$(mktemp -d "${TMPDIR:-/tmp}/ais_keylen.XXXXXX") || exit 2
K300=$(awk 'BEGIN{while(i++<300)printf "k"}')
K255=$(awk 'BEGIN{while(i++<255)printf "a"}')
"$AIS" -f "$KL" -v good venice >/dev/null
kout=$("$AIS" -f "$KL" -v note "$K300" venice 2>&1); krc=$?
okeq    "keylen: a 300-byte key is refused"             "1" "$krc"
ok      "keylen: and the message names the limit"       "the limit is 255" "$kout"
ok      "keylen: and how long the key was"              "300 bytes" "$kout"
okeq    "keylen: the store kept its one line"           "1" "$(grep -c . "$KL/store")"
okeq    "keylen: next_id did not move"                  "2" "$(cat "$KL/next_id")"
"$AIS" -f "$KL" --compact -y >/dev/null 2>&1
okeq    "keylen: and compaction still succeeds"         "0" "$?"
ok      "keylen: a 255-byte key is accepted"            "fits" \
        "$("$AIS" -f "$KL" -v fits "$K255" >/dev/null 2>&1; "$AIS" -f "$KL" "$K255")"

# The same line arriving by --import is skipped and counted, never half-written.
KI=$(mktemp -d "${TMPDIR:-/tmp}/ais_keylen_i.XXXXXX") || exit 2
iout=$(printf '%s venice -v note\ngood -v other\n' "$K300" | "$AIS" -f "$KI" --import 2>&1)
ok      "keylen: --import skips the long-key line"      "skipped (key too long" "$iout"
ok      "keylen: and counts it as skipped"              "imported 1, skipped 1" "$iout"
okeq    "keylen: only the good line was written"        "1" "$(grep -c . "$KI/store")"
rm -rf "$KI"

# The same, as a peer's A| line: that path spools into a batch, where a refused
# put would otherwise be dropped without a word.
KS=$(mktemp -d "${TMPDIR:-/tmp}/ais_keylen_s.XXXXXX") || exit 2
sout=$(printf 'A|2026-01-02T03:04:05Z|%s venice|note\nA|2026-01-02T03:04:06Z|good|other\n' "$K300" \
       | "$AIS" -f "$KS" --import 2>&1)
ok      "keylen: a peer's A| line is skipped too"       "skipped (key too long" "$sout"
ok      "keylen: and counted, not silently dropped"     "imported 1, skipped 1" "$sout"
okempty "keylen: it left no record under its other key" "$("$AIS" -f "$KS" venice 2>/dev/null)"
ok      "keylen: the good line still arrived"           "other" "$("$AIS" -f "$KS" good)"
rm -rf "$KS"

# The MCP save tool bounds the length too: an agent's key is a filename as well.
mout=$(printf '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"save","arguments":{"value":"agent value","keys":["%s"]}}}\n' "$K300" \
       | "$AIS" -f "$KL" --mcp rw 2>/dev/null)
jsonok  "keylen: the MCP refusal is valid JSON"         "$mout"
ok      "keylen: MCP save names the cap"                "at most 255 bytes" "$mout"
ok      "keylen: and marks it a tool error"             '"isError":true' "$mout"
okempty "keylen: nothing was stored"                    "$("$AIS" -f "$KL" --find 'agent value' 2>/dev/null)"
rm -rf "$KL"

echo "---- $pass passed, $fail failed"
[ "$fail" -eq 0 ]
