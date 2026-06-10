#!/bin/sh
# example.sh -- build the shipped example/demo index from the CURRENT docs.
# It is a real AIS index (plain files), so a user can inspect it to learn the
# format (ls example/idx, cat example/store, example/blobs/) and tour the app
# before storing anything. Rebuilt fresh; it is never the user's personal index.
set -e
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

AIS=${AIS:-c/ais}
[ -x "$AIS" ] || AIS=ais          # fall back to an installed ais
EX=example
rm -rf "$EX"

# The docs themselves, as documents (multi-line -> blobs). Keys: 'about', 'docs'
# ('doc' is a command, so it is not used as a key).
"$AIS" -f "$EX" --doc about docs  < doc/about.txt >/dev/null
"$AIS" -f "$EX" --doc docs readme < README.md     >/dev/null

# A few records that show the key model: one item filed under several keys is a
# small graph, recalled by any of them.
"$AIS" -f "$EX" -v "https://en.wikipedia.org/wiki/Memex" memex history reference >/dev/null
"$AIS" -f "$EX" -v "keys are words you choose, not folders you nest" tip example >/dev/null
"$AIS" -f "$EX" -v "https://gavr144.substack.com/p/intelligence-is-the-discovery-of" article ai compression >/dev/null

echo "built $EX/  (inspect: ls $EX; cat $EX/store; ls $EX/idx; '$AIS' -f $EX serve)"
