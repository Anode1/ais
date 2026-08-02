#!/bin/sh
# matrix.sh -- generate the coverage matrix from the code, never from a plan.
# Cells come from the parser table, the route table and the widget tree, so a
# surface that exists but nobody tested shows up as an empty cell, not as an
# unknown unknown. Read-only: it prints TSV and touches nothing.
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
main=$root/c/main.c
serve=$root/c/serve.c
help=$root/c/help.c
dart=$root/app/flutter/lib

# where a test for each surface could plausibly live
cli_tests="$root/tests/cli.sh $root/c/tests.c"
web_tests=$(ls "$root"/tests/gui/*.sh "$root"/tests/gui/*.c 2>/dev/null || true)
app_tests=$(ls "$root"/app/flutter/test/*.dart "$root"/app/flutter/uitest/* "$root"/tests/gui/flutter-sync*.sh 2>/dev/null || true)

# risk is a default, not a verdict: the fleet may raise a cell, never silently lower it
risk_of() {
    case "$1" in
    *sync*|*merge*|*import*|*export*|*folder*|*token*|*compact*|*forget*) echo HIGH ;;
    *del*|*untag*|*secret*|*-e*|*doc*|*store*|*crypto*|*reveal*)          echo HIGH ;;
    *add*|*set*|*update*|*put*|*save*|*init*|*switch*|*project*)          echo MED ;;
    *get*|*find*|*timeline*|*tags*|*keys*|*dump*)                         echo MED ;;
    *)                                                                    echo LOW ;;
    esac
}

# evidence = the first test file that names the cell; a mention is a lead, not proof
evidence_for() {
    tok=$1; shift
    for f in "$@"; do
        [ -f "$f" ] || continue
        n=$(grep -nF -- "$tok" "$f" 2>/dev/null | head -1 | cut -d: -f1) || true
        if [ -n "${n:-}" ]; then printf '%s:%s\n' "${f#$root/}" "$n"; return; fi
    done
    echo -
}

emit() {
    surface=$1; cell=$2; ev=$3
    st=GAP?; [ "$ev" = - ] || st=COVERED?
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$surface/$cell" "$surface" "$cell" "$(risk_of "$cell")" "$st" - "$ev"
}

printf 'ID\tSURFACE\tCELL\tRISK\tSTATUS\tOWNER\tEVIDENCE\n'

# 1. CLI long options -- the getopt_long table is the ground truth, not the help text
sed -n '/static const struct option longopts/,/};/p' "$main" |
sed -n 's/.*{ *"\([a-z0-9-]*\)".*/\1/p' | sort -u |
while read -r v; do
    [ -n "$v" ] || continue
    emit cli "--$v" "$(evidence_for "--$v" $cli_tests)"
done

# 2. CLI short flags -- taken from the getopt_long spec string itself
sed -n 's/.*getopt_long(argc, argv, "\([^"]*\)".*/\1/p' "$main" | head -1 |
sed 's/://g' | fold -w1 |
while read -r c; do
    [ -n "$c" ] || continue
    emit cli "-$c" "$(evidence_for "-$c " $cli_tests)"
done

# 3. web API -- method and path are matched on one line in the dispatch chain
grep -oE 'strcmp\(method, "[A-Z]+"\) == 0 && strcmp\(path, "/api/[a-z/-]*"\)' "$serve" |
sed -E 's/strcmp\(method, "([A-Z]+)"\) == 0 && strcmp\(path, "([^"]*)"\)/\1 \2/' | sort -u |
while read -r m p; do
    emit web "$m $p" "$(evidence_for "$p" $web_tests)"
done

# 4. Android UI -- every label a user can actually see or press
grep -hoE "(title|label|tooltip): *(const )?Text\('[^']+'\)|tooltip: *'[^']+'|label: *'[^']+'" "$dart"/*.dart 2>/dev/null |
sed -E "s/.*'([^']+)'.*/\1/" | sed 's/\$[a-zA-Z_]*//g' | sort -u |
while read -r l; do
    case "$l" in ''|*'{'*) continue ;; esac
    emit android "$l" "$(evidence_for "$l" $app_tests)"
done

# 5. Android keyboard shortcuts -- a moved control must not silently unwire a test
grep -hoE 'LogicalKeyboardKey\.[a-zA-Z0-9]+' "$dart"/*.dart 2>/dev/null | sort -u |
while read -r k; do
    emit android "key:${k#LogicalKeyboardKey.}" "$(evidence_for "$k" $app_tests)"
done

# 6. doc drift -- a verb the help text promises that the parser does not accept
tbl=$(sed -n '/static const struct option longopts/,/};/p' "$main" | sed -n 's/.*{ *"\([a-z0-9-]*\)".*/\1/p' | sort -u)
grep -h 'ais --' "$help" | grep -ohE '\-\-[a-z][a-z0-9-]+' | sed 's/^--//' | sort -u |
while read -r v; do
    printf '%s\n' "$tbl" | grep -qx "$v" && continue
    printf 'doc/--%s\tdoc\t--%s\tHIGH\tDRIFT\t-\thelp.c promises it, longopts[] does not accept it\n' "$v" "$v"
done
