#!/bin/sh
# demo.sh -- drive a scripted terminal demo of ais, for asciinema.
#
# It types each command out, runs it for real against a throwaway index in a
# temp directory, and pauses so a viewer can read the output. Nothing touches
# the user's own index and nothing opens a window.
#
#   asciinema rec -c scripts/demo.sh --overwrite ais.cast   # record
#   DEMO_SECRET=1 asciinema rec -c scripts/demo.sh --overwrite ais.cast
#                                       # ...including the encrypted step, which
#                                       # stops for you to type a secret twice
#   asciinema play ais.cast                                 # check it
#   agg ais.cast ais.gif                                    # optional GIF
#
# Tuning: SPEED is the per-character typing delay, BEAT the pause after output.
#   SPEED=0 BEAT=0 scripts/demo.sh     # instant, for checking the script itself
#
# Deliberately plain: no colour, no bold, no cursor tricks. The tool prints
# text and the recording should look like a terminal, not like a demo reel.
# Record in a sober terminal at about 92x28 and leave the player theme default.
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

# Type a line one character at a time, then run it. The prompt and the typing
# are printed by us: this is a real shell, but a scripted one.
type_run() {
    printf '%s' "$PROMPT"
    if [ "$SPEED" = 0 ]; then
        printf '%s' "$1"
    else
        # `|| [ -n "$_c" ]` so the last character, which fold leaves without a
        # trailing newline, is still printed.
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
type_run "ais -v ~/photos/IMG_3920.jpg italy venice 2023"

say 'or pipe a filtered list in, tagging them all at once'
type_run "ls photos/*.jpg | ais -v - italy photos"

say 'links, notes and the command you always forget'
type_run "ais -v https://en.wikipedia.org/wiki/Memex memex reference"
type_run "ais -v 'ssh -N -L 8080:localhost:5432 deploy@10.0.0.7' tunnel uat"
type_run "is tunnel uat"

say 'the git incantation you look up every single time'
type_run "ais -v 'git rev-list --left-right --count origin/main...HEAD' git ahead"
type_run "is git"

say 'recall: bare words are keys, no flags to remember'
type_run "is italy venice"

say 'AND is the default; -o is OR'
type_run "is -o venice memex"

say 'every key, busiest first'
type_run "ais --tags"

# The one interactive step, OFF by default so a plain run never blocks: ais
# prompts for the secret and a passphrase, both with echo off, reading /dev/tty.
# Under `asciinema rec -c` that tty is the recording session, so you type them
# live and the cast shows the prompts, not the keystrokes. Turn it on with
# DEMO_SECRET=1 when you are recording and ready to type.
if [ "${DEMO_SECRET:-0}" = 1 ]; then
    say 'a password lives next to what it belongs to, encrypted'
    type_run "ais uat login -e"
    say 'piped or dumped it stays opaque: an agent reading the index sees this'
    type_run "is uat login | cat"
fi

say 'and it is all plain text on your disk, greppable and repairable'
type_run "head -3 .ais/store"

say 'that is it: file it under your keys, get it back by them'
sleep 2
