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
st=$("$AIS" -f "$AD" --stats)
if [ -n "$st" ]; then pass=$((pass + 1)); echo "  ok   stats: prints a summary"
else fail=$((fail + 1)); echo "  FAIL stats: empty"; fi
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
"$AIS" -f "$CP" -y --del "$cid" >/dev/null
"$AIS" -f "$CP" -y --compact >/dev/null
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
rm -rf "$UP"

# 17. saved default index persists in ~/.ais/config ACROSS PROCESSES. Each ais
#     call is a fresh process, so reading the path back -- and resolving --where
#     to it from a dir with no local .ais -- proves it was written to disk, not
#     held in memory. Idempotent: saving the same path twice is stable.
#     --default writes the real ~/.ais/config (home is the OS account dir, not a
#     redirectable env var), so snapshot it and restore on exit.
CFG="$HOME/.ais/config"
CFGBAK="$DIR/config.orig"; HADCFG=no
[ -f "$CFG" ] && { cp "$CFG" "$CFGBAK"; HADCFG=yes; }
restore_cfg() { if [ "$HADCFG" = yes ]; then cp "$CFGBAK" "$CFG"; else rm -f "$CFG"; fi; }
# restore BEFORE removing $DIR -- the backup (CFGBAK) lives inside it.
trap 'restore_cfg; rm -rf "$DIR"' EXIT

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

echo "---- $pass passed, $fail failed"
[ "$fail" -eq 0 ]
