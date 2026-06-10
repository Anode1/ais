#!/bin/sh
# cli.sh -- end-to-end tests of the ais BINARY.
#
# These reach what the C unit tests (which call the ais.h API directly) cannot:
# the streaming stdin path (`put -`), real pipelines, argv handling, and that an
# inserted key is genuinely present in the index afterwards. POSIX sh, so it
# runs unchanged on Linux and macOS.
#
# Usage:  sh tests/cli.sh [path-to-ais]      (default ./c/ais)

AIS=${1:-./c/ais}
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
printf 'alpha\nbeta\ngamma\n' | "$AIS" -f "$DIR" put - greek
out=$("$AIS" -f "$DIR" greek)
ok    "stdin: first piped line stored"  "alpha" "$out"
ok    "stdin: last piped line stored"   "gamma" "$out"
n=$(printf '%s\n' "$out" | grep -c .)
okeq  "stdin: exactly 3 records"        "3" "$n"

# ... and the key itself is now present in the index
keys=$("$AIS" -f "$DIR" keys)
ok    "stdin: key 'greek' is in the database" "greek" "$keys"

# 2. self-indexing round-trip: pipe the ais binary's own path in, then get it
#    back by key AND find it by content -- two checks in one pipeline.
find "$AIS" | "$AIS" -f "$DIR" put - executable
out=$("$AIS" -f "$DIR" executable)
ok    "self-index: binary path stored under 'executable'" "ais" "$out"
out=$("$AIS" -f "$DIR" find ais)
ok    "self-index: 'find ais' locates the path"           "ais" "$out"
keys=$("$AIS" -f "$DIR" keys)
ok    "self-index: key 'executable' is in the database"   "executable" "$keys"

# 3. find is content (the value), not tags (the key)
printf 'venice is sinking\n' | "$AIS" -f "$DIR" put - trip
out=$("$AIS" -f "$DIR" find venice)
ok      "find: matches a value substring"     "venice is sinking" "$out"
out=$("$AIS" -f "$DIR" find nosuchword)
okempty "find: absent term prints nothing"    "$out"

# 4. git-style location: `init` creates a local .ais, found from subdirectories
TREE=$(mktemp -d "${TMPDIR:-/tmp}/ais_tree.XXXXXX") || exit 2
( cd "$TREE" && AIS_INDEX= "$AIS" init >/dev/null )
if [ -d "$TREE/.ais" ]; then
    pass=$((pass + 1)); echo "  ok   init: creates .ais in the current dir"
else
    fail=$((fail + 1)); echo "  FAIL init: no .ais created"
fi
mkdir -p "$TREE/sub/deep"
( cd "$TREE/sub/deep" && printf 'a note\n' | AIS_INDEX= "$AIS" put - memo >/dev/null )
out=$(cd "$TREE" && AIS_INDEX= "$AIS" memo)
ok "discovery: a put from a subdir walked up to .ais" "a note" "$out"
keys=$(cd "$TREE/sub" && AIS_INDEX= "$AIS" keys)
ok "discovery: key visible from another subdir"       "memo"   "$keys"
rm -rf "$TREE"

# 5. global fallback: no -f, no .ais -> $XDG_DATA_HOME/ais (auto-created, isolated)
G=$(mktemp -d "${TMPDIR:-/tmp}/ais_glob.XXXXXX") || exit 2
( cd "$G" && AIS_INDEX= XDG_DATA_HOME="$G/xdg" "$AIS" put global-note gk >/dev/null )
if [ -d "$G/xdg/ais" ]; then
    pass=$((pass + 1)); echo "  ok   global: \$XDG_DATA_HOME/ais auto-created"
else
    fail=$((fail + 1)); echo "  FAIL global: \$XDG_DATA_HOME/ais not created"
fi
out=$(cd "$G" && AIS_INDEX= XDG_DATA_HOME="$G/xdg" "$AIS" gk)
ok "global: value retrievable from the per-user index" "global-note" "$out"
rm -rf "$G"

# 6. path-relativization: `put -R` stores paths relative to the .ais worktree
#    root, so they survive the whole tree being moved.
W=$(mktemp -d "${TMPDIR:-/tmp}/ais_rel.XXXXXX") || exit 2
( cd "$W" && AIS_INDEX= "$AIS" init >/dev/null && mkdir -p docs \
    && echo hi > docs/a.txt && AIS_INDEX= "$AIS" put -R docs papers >/dev/null )
out=$(cd "$W" && AIS_INDEX= "$AIS" papers)
ok "relativize: stored path is relative to root" "docs/a.txt" "$out"
case "$out" in
    *"$W"*) fail=$((fail + 1)); echo "  FAIL relativize: absolute prefix leaked: $out" ;;
    *)      pass=$((pass + 1)); echo "  ok   relativize: no absolute prefix in the value" ;;
esac
# move the whole tree (.ais goes with it); the relative ref is unchanged
mv "$W" "${W}_moved"
out=$(cd "${W}_moved" && AIS_INDEX= "$AIS" papers)
ok "relativize: value survives moving the tree" "docs/a.txt" "$out"
rm -rf "${W}_moved"

# 7. a global (non-.ais) index has no worktree root -> paths stored absolute
A=$(mktemp -d "${TMPDIR:-/tmp}/ais_abs.XXXXXX") || exit 2
mkdir -p "$A/data" && echo x > "$A/data/f.txt"
( cd "$A" && AIS_INDEX= XDG_DATA_HOME="$A/xdg" "$AIS" put -R data files >/dev/null )
out=$(cd "$A" && AIS_INDEX= XDG_DATA_HOME="$A/xdg" "$AIS" files)
case "$out" in
    *"|/"*) pass=$((pass + 1)); echo "  ok   global: put -R stores an absolute path" ;;
    *)      fail=$((fail + 1)); echo "  FAIL global: expected absolute path, got: $out" ;;
esac
rm -rf "$A"

# 8. immutability: del needs confirmation; no input aborts, -y bypasses
DD=$(mktemp -d "${TMPDIR:-/tmp}/ais_del.XXXXXX") || exit 2
id=$("$AIS" -f "$DD" put "scratch value" tmp)
"$AIS" -f "$DD" del "$id" </dev/null >/dev/null 2>&1     # no -y, EOF -> aborted
out=$("$AIS" -f "$DD" tmp)
ok      "guard: del without confirmation is refused" "scratch value" "$out"
"$AIS" -f "$DD" -y del "$id" >/dev/null                  # -y -> deletes
out=$("$AIS" -f "$DD" tmp)
okempty "guard: del -y removes the record" "$out"
rm -rf "$DD"

# 9. interactive: stdin = values, keys read per line from $AIS_TTY (scripted tty)
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

echo "---- $pass passed, $fail failed"
[ "$fail" -eq 0 ]
