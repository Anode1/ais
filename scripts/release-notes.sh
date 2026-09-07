#!/bin/sh
# release-notes.sh TAG -- prepend a scaffold entry for TAG to doc/RELEASE_NOTES.md
# from the commit subjects since the previous tag, newest first. Run it BEFORE
# tagging (the range then ends at HEAD), curate the wording, commit, then tag.
# An existing entry for TAG is left alone. Backfill: run it per tag, oldest first.
set -e
tag=${1:?usage: release-notes.sh vX.Y.Z}
root=$(git rev-parse --show-toplevel)
notes="$root/doc/RELEASE_NOTES.md"
[ -f "$notes" ] || { echo "no $notes" >&2; exit 1; }
if grep -q "^## $tag " "$notes"; then
    echo "$notes already has $tag"; exit 0
fi
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    end=$tag
    prev=$(git describe --tags --abbrev=0 "$tag^" 2>/dev/null || true)
    date=$(git log -1 --format=%ad --date=short "$tag")
else
    end=HEAD
    prev=$(git describe --tags --abbrev=0 2>/dev/null || true)
    date=$(date +%Y-%m-%d)
fi
range=${prev:+$prev..}$end
tmp=$(mktemp)
{
    # header: everything above the first entry
    awk '/^## /{exit} {print}' "$notes"
    printf '## %s (%s)\n\n' "$tag" "$date"
    git log --format='- %s' --no-merges "$range" \
        | grep -v -i -e '^- release notes' -e '^- roadmap:' -e '^- doc cleaning' -e '^- release: v' \
        || true
    printf '\n'
    awk 'f{print} /^## /{if(!f){f=1;print}}' "$notes"
} > "$tmp"
mv "$tmp" "$notes"
echo "added $tag ($range) to $notes"
