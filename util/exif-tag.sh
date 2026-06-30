#!/bin/sh
# exif-tag.sh -- file every JPEG under DIR into an ais index, keyed by its EXIF
# capture date (YYYY-MM-DD) and year (YYYY), plus any extra keys you pass. The
# photo's path is stored as the value, so the image is never copied, and
# re-running is idempotent (ais ignores a value already filed under the keys).
# Photos with no EXIF date are filed under the extra keys only, and listed so
# you can spot them.
#
#   util/exif-tag.sh [-f INDEX] DIR [EXTRA_KEY ...]
#   util/exif-tag.sh -f ~/.ais-photos ~/photos/venice italy venice
#
# Needs ./exif built (run `make` in util/) and ais on PATH.
set -u

ais_index=
while getopts f: opt; do
    case $opt in
        f) ais_index=$OPTARG ;;
        *) echo "usage: exif-tag.sh [-f INDEX] DIR [EXTRA_KEY ...]" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || { echo "usage: exif-tag.sh [-f INDEX] DIR [EXTRA_KEY ...]" >&2; exit 2; }

dir=$1; shift
extra="$*"                                   # remaining args are extra keys (word-split below)
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exif=$here/exif
[ -d "$dir" ] || { echo "exif-tag: not a directory: $dir" >&2; exit 2; }
[ -x "$exif" ] || { echo "exif-tag: build the reader first: (cd $here && make)" >&2; exit 2; }
command -v ais >/dev/null 2>&1 || { echo "exif-tag: ais is not on PATH" >&2; exit 2; }

file_one() {                                 # file_one VALUE KEY...
    v=$1; shift
    if [ -n "$ais_index" ]; then ais -f "$ais_index" -v "$v" "$@" >/dev/null
    else                         ais -v "$v" "$@" >/dev/null
    fi
}

list=$(mktemp); trap 'rm -f "$list"' EXIT
find -L "$dir" -type f \( -iname '*.jpg' -o -iname '*.jpeg' \) > "$list"

tagged=0; nodate=0
while IFS= read -r photo; do
    date=$("$exif" "$photo" 2>/dev/null | awk -F'\t' '$1=="date"{print $2}')
    if [ -n "$date" ]; then
        # shellcheck disable=SC2086  -- extra keys are intentionally word-split
        file_one "$photo" "${date%%-*}" "$date" $extra && { tagged=$((tagged+1)); echo "  $date  $photo"; }
    else
        # shellcheck disable=SC2086
        file_one "$photo" $extra && { nodate=$((nodate+1)); echo "  ......      $photo"; }
    fi
done < "$list"

echo "filed $tagged dated, $nodate undated"
