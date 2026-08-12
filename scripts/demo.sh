#!/bin/sh
# demo.sh -- drive a scripted terminal demo of ais, for asciinema.
#
# It types each command out and runs it for real against a throwaway index in a
# temp directory. Nothing touches the user's own index; nothing opens a window.
#
#   asciinema rec -c scripts/demo.sh --overwrite ais.cast   # record
#   DEMO_SECRET=1 asciinema rec -c scripts/demo.sh --overwrite ais.cast
#                                       # ...including the encrypted step, which
#                                       # stops for you to type a secret twice
#   asciinema play ais.cast                                 # check it
#   agg ais.cast ais.gif                                    # optional GIF
#
# SPEED is the per-character typing delay, BEAT the pause after output.
#   SPEED=0 BEAT=0 scripts/demo.sh     # instant, for checking the script itself
#
# Deliberately plain: no colour, no bold, no cursor tricks. Record in a sober
# terminal at about 92x28 and leave the player theme default.
set -e
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

AIS=${AIS:-c/ais}
[ -x "$AIS" ] || AIS=ais           # fall back to an installed ais
AIS=$(command -v "$AIS" || printf '%s/%s' "$PWD" "$AIS")

SPEED=${SPEED:-0.035}              # seconds per typed character
BEAT=${BEAT:-1.4}                  # pause after a command's output
PROMPT=${PROMPT:-'$ '}

DEMO=$(mktemp -d "${TMPDIR:-/tmp}/ais_demo.XXXXXX") || exit 2
trap 'rm -rf "$DEMO"' EXIT INT TERM

# Files to point the index at, so the paths in the demo are real.
mkdir -p "$DEMO/photos"
: > "$DEMO/photos/IMG_3921.jpg"     # 3920 is filed by hand below, so the piped
: > "$DEMO/photos/IMG_4002.jpg"     # batch covers different files, not a repeat
cd "$DEMO"

pause() { [ "$BEAT" = 0 ] || sleep "$BEAT"; }

# Type a line one character at a time, then run it: a real shell, scripted.
type_run() {
    printf '%s' "$PROMPT"
    if [ "$SPEED" = 0 ]; then
        printf '%s' "$1"
    else
        # `|| [ -n "$_c" ]`: fold leaves the last character without a newline.
        printf '%s' "$1" | fold -w1 | while IFS= read -r _c || [ -n "$_c" ]; do
            printf '%s' "$_c"; sleep "$SPEED"
        done
    fi
    printf '\n'
    sleep 0.25
    eval "$1" || true
    pause
}

# A comment line, to narrate without running anything.
say() { printf '%s# %s\n' "$PROMPT" "$1"; sleep 1; }

clear

say 'an index of your own keys, in plain text'
type_run "ais --init"

say 'one alias, and recall costs two characters'
type_run "alias is=ais"

say 'file a photo under the words you would actually think of later'
type_run "is italy venice 2023 -v ~/photos/IMG_3920.jpg"

say 'or pipe a filtered list in, tagging them all at once'
type_run "ls photos/*.jpg | is italy photos -v -"

say 'the ssh tunnels that used to live in a comment at the top of a config file'
type_run "is tunnel dev remote -v 'ssh -N -L 3307:127.0.0.1:3306 ubuntu@203.0.113.10'"
type_run "is tunnel uat remote -v 'ssh -N -L 3308:db-uat.example.com:3306 uat'"

say 'two keys pick the one you meant'
type_run "is tunnel uat"

say 'one key gathers them: every way I reach a machine that is not this one'
type_run "is remote"

say 'links and notes share the same index'
type_run "is memex reference -v https://en.wikipedia.org/wiki/Memex"

say 'the git incantation you look up every single time'
type_run "is git ahead -v 'git rev-list --left-right --count origin/main...HEAD'"
type_run "is git"

say 'recall: bare words are keys, no flags to remember'
type_run "is italy venice"

say 'AND is the default; -o is OR'
type_run "is -o venice memex"

say 'every key, busiest first'
type_run "is --tags"

# The one interactive step, OFF by default so a plain run never blocks: ais reads
# the secret and passphrase from /dev/tty with echo off. Under `asciinema rec -c`
# that tty is the recording session, so you type them live.
if [ "${DEMO_SECRET:-0}" = 1 ]; then
    say 'a password lives next to what it belongs to, encrypted'
    type_run "is uat login -e"
    say 'piped or dumped it stays opaque: an agent reading the index sees this'
    type_run "is uat login | cat"
fi

say 'and it is all plain text on your disk, greppable and repairable'
type_run "head -3 .ais/store"

say 'that is it: file it under your keys, get it back by them'
sleep 2
