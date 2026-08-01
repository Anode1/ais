#!/bin/sh
# run.sh -- assert the engine still fits the FFI seam's stack budget.
#
# WHY THIS EXISTS. The Flutter app calls the engine from a Dart isolate thread,
# not from main(), and that thread's stack is a fraction of a process's default.
# c/ais.c names 512 KB for it. A change once doubled ais_put_at's frame and took
# the primary save path past that figure; every test in the suite stayed green,
# because none of them measured stack. On a phone that is not a test failure, it
# is the app dying when the user saves a note.
#
# ais.c's own comment is the budget, so keep the two in step if it ever moves.
#
# Exit 0 = pass, 1 = fail, 77 = SKIP (nothing built to link against).

set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
here="$root/tests/stack"

BUDGET_KB=512          # what the FFI seam gives us (c/ais.c)
#
# A limit PER PATH, just above what each needs today, not one loose number near
# the budget: a single 448 KB bar let a deliberate 128 KB regression on the save
# path pass, because that path only needs ~256 KB and had the whole budget to
# grow into before anything complained. The point is to catch the growth on the
# way, while there is still room to land -- so each of these should sit roughly
# 64 KB above its measured floor. If one trips, either the path grew a buffer or
# it genuinely needs more; raise the number DELIBERATELY, never reflexively, and
# never past BUDGET_KB.

[ -f "$root/c/embed.o" ] || { echo "  SKIP c/ not built (run make first)"; exit 77; }

objs=$(ls "$root"/c/*.o 2>/dev/null | grep -v '/main\.o$' | grep -v '/tests\.o$' | tr '\n' ' ')
cobjs=$(ls "$root"/c/crypto/*.o 2>/dev/null | tr '\n' ' ')
[ -n "$objs" ] || { echo "  SKIP no engine objects"; exit 77; }

work=$(mktemp -d "${TMPDIR:-/tmp}/ais_stack.XXXXXX")
bin="$work/ffi_stack"
cleanup() { rm -rf "$work"; }
trap cleanup EXIT

if ! cc -std=c99 -O2 -I"$root/c" "$here/ffi_stack.c" $objs $cobjs -o "$bin" -lpthread 2>"$work/cc.log"; then
    echo "  SKIP cannot link the harness (no pthread?)"
    sed 's/^/       /' "$work/cc.log" | head -5
    exit 77
fi

# A seeded index, and a shared folder holding a peer bundle to import.
src="$work/src"; folder="$work/folder"
mkdir -p "$folder"
"$root/c/ais" -f "$src" --init >/dev/null
i=1
while [ $i -le 40 ]; do
    "$root/c/ais" -f "$src" -v "http://x/$i" alpha beta >/dev/null
    i=$((i + 1))
done
"$root/c/ais" -f "$src" -v "http://x/1" gamma >/dev/null      # a post-creation attach (T|)
"$root/c/ais" -f "$src" --del 5 -y >/dev/null                 # and a delete (D|)
"$root/c/ais" -f "$src" --sync-folder "$folder" >/dev/null

rc=0
check() {                       # check <mode> <label> <limit-KB>
    mode=$1; label=$2; limit=$3; idx="$work/idx.$mode"
    rm -rf "$idx"
    if [ "$mode" = resurrect ]; then
        "$root/c/ais" -f "$idx" --init >/dev/null
        "$root/c/ais" -f "$idx" -v "http://x/stacktest" alpha >/dev/null
        "$root/c/ais" -f "$idx" --del 1 -y >/dev/null
        m=store
    else
        "$root/c/ais" -f "$idx" --init >/dev/null
        m=$mode
    fi
    if "$bin" "$limit" "$idx" "$folder" "$m" >/dev/null 2>&1; then
        echo "  ok   $label fits ${limit}KB (budget ${BUDGET_KB}KB)"
    else
        echo "  FAIL $label needs more than ${limit}KB of stack"
        echo "       The FFI seam gives ~${BUDGET_KB}KB (see c/ais.c). Look for a new"
        echo "       AIS_LINE_MAX buffer on this path; one key is AIS_KEY_MAX."
        rc=1
    fi
}

# Measured floors today: 224 / 288 / 352 KB. Each limit is one 64 KB step above
# its floor -- exactly one AIS_LINE_MAX buffer of room -- so re-adding a
# line-sized buffer to any of these paths trips it, which is the regression that
# went unnoticed and the whole reason this exists.
check store     "save a record"            272   # the primary command  (floor 224)
check resurrect "re-save a deleted record" 336   # the longer save branch (floor 288)
check folder    "import a shared folder"   400   # the app's auto-sync   (floor 352)

exit $rc
