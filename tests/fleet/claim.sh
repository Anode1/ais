#!/bin/sh
# claim.sh -- atomic claim / write-back on the matrix, so N workers can share one file.
# mkdir is the lock: atomic on every filesystem, and a crashed worker leaves a
# directory you can see and remove, not a mystery.
#
#   claim.sh next  <matrix> <owner> [RISK]   print one TODO row's ID and mark it CLAIMED
#   claim.sh set   <matrix> <id> <status> <evidence>   write a verdict back
#   claim.sh stat  <matrix>                  counts by status (the termination check)
set -eu

lock() {
    i=0
    while ! mkdir "$1.lock" 2>/dev/null; do
        i=$((i+1)); [ "$i" -lt 300 ] || { echo "claim.sh: stale lock $1.lock" >&2; exit 1; }
        sleep 0.1
    done
}
unlock() { rmdir "$1.lock" 2>/dev/null || true; }

cmd=${1:?usage: claim.sh next|set|stat}; m=${2:?matrix path}

case $cmd in
next)
    owner=${3:?owner name}; want=${4:-}
    lock "$m"; trap 'unlock "$m"' EXIT
    # highest risk first, and only rows nobody holds
    id=$(awk -F'\t' -v w="$want" '
        NR>1 && $6=="-" && ($5=="GAP?"||$5=="COVERED?"||$5=="TODO") &&
        (w==""||$4==w) { print $4"\t"$1 }' "$m" |
        sort | awk -F'\t' '$1=="HIGH"{print;exit}' | cut -f2)
    [ -n "$id" ] || id=$(awk -F'\t' -v w="$want" '
        NR>1 && $6=="-" && ($5=="GAP?"||$5=="COVERED?"||$5=="TODO") &&
        (w==""||$4==w) { print $1; exit }' "$m")
    [ -n "$id" ] || { echo "" ; exit 0; }
    awk -F'\t' -v OFS='\t' -v id="$id" -v o="$owner" \
        '$1==id{$6=o} {print}' "$m" > "$m.tmp" && mv "$m.tmp" "$m"
    echo "$id"
    ;;
set)
    id=${3:?cell id}; st=${4:?status}; ev=${5:--}
    lock "$m"; trap 'unlock "$m"' EXIT
    awk -F'\t' -v OFS='\t' -v id="$id" -v s="$st" -v e="$ev" \
        '$1==id{$5=s; $7=e} {print}' "$m" > "$m.tmp" && mv "$m.tmp" "$m"
    ;;
stat)
    awk -F'\t' 'NR>1{c[$5]++; if($4=="HIGH"&&$5!="COVERED")h++} END{
        for(k in c) printf "%-10s %d\n", k, c[k]
        printf "%-10s %d\n", "HIGH-open", h+0
    }' "$m"
    ;;
*) echo "claim.sh: unknown command $cmd" >&2; exit 2 ;;
esac
