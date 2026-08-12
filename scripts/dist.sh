#!/bin/sh
# dist.sh -- build release bundles into releases/<platform>/ (kept across runs).
#   make dist       this platform's BINARY bundle + the SOURCE bundle
#   make dist-src   just the source bundle (build anywhere)
#
# Every bundle is a .zip so one tool opens any download on any OS. One bundle per
# platform serves both the CLI and the GUI launcher, which wraps the same binary:
#   ais-<ver>-src.zip                 source
#   ais-<ver>-<os>-<arch>.zip         binary  (linux, macos)
# Each gets a same-named .md5 sidecar, OUTSIDE the artifact.
#
# One machine cannot cross-build the others -> run `make dist` on each. Windows is
# not built here; the native MinGW build is CI-validated only.
set -e
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION=$(git describe --tags --always --dirty 2>/dev/null | sed 's/^v//')
[ -n "$VERSION" ] || VERSION=0.0.0-dev   # single source = the git tag (see c/Makefile)
mkdir -p releases

# same-named .md5 next to the package file $1 (verify the download).
sidecar() {
    d=$(dirname "$1"); f=$(basename "$1")
    ( cd "$d" && { md5sum "$f" 2>/dev/null || md5 -r "$f"; } > "$f.md5" )
}

build_src() {
    name="ais-$VERSION-src"; out="releases/src"; stage="$out/$name"
    mkdir -p "$out"; rm -rf "$stage"; mkdir -p "$stage"
    for item in Makefile README.md COPYING \
                c doc gui man scripts tests; do
        [ -e "$item" ] && cp -R "$item" "$stage/"
    done
    rm -f "$stage"/c/*.o "$stage"/c/*.d "$stage"/c/ais "$stage"/c/ais_ut \
          "$stage"/ais "$stage"/tests/INDEX/tomb "$stage"/tests/INDEX/lock 2>/dev/null || true
    if command -v zip >/dev/null 2>&1; then
        ( cd "$out" && rm -f "$name.zip" && zip -rq "$name.zip" "$name" ); pkg="$out/$name.zip"
    else
        tar -C "$out" -czf "$out/$name.tar.gz" "$name"; pkg="$out/$name.tar.gz"
        echo "dist: 'zip' not found, made $name.tar.gz instead (install zip for .zip)"
    fi
    rm -rf "$stage"; sidecar "$pkg"
    echo "built $pkg (+ .md5)   [unpack, cd $name, then: make]"
}

build_bin() {
    os=$(uname -s | tr 'A-Z' 'a-z'); arch=$(uname -m)
    # clean build so flags take (a stale c/ais would not relink as static).
    case "$os" in
        linux)  make -C c clean >/dev/null && make -C c static >/dev/null
                pretty=linux; launcher=gui/ais-web.desktop ;;
        darwin) make -C c clean >/dev/null && make -C c >/dev/null
                pretty=macos; launcher=gui/ais-web.command ;;
        *) echo "dist: no binary for '$os' (Windows ships the native installer; see release.yml)"; return 0 ;;
    esac
    name="ais-$VERSION-$pretty-$arch"; out="releases/$pretty"; stage="$out/$name"
    mkdir -p "$out"; rm -rf "$stage"; mkdir -p "$stage"

    cp c/ais "$stage/ais"
    [ -f COPYING ]      && cp COPYING      "$stage/"
    [ -f doc/about.txt ] && cp doc/about.txt "$stage/"
    [ -f doc/USING.txt ] && cp doc/USING.txt "$stage/"
    [ -f man/ais.1 ] && { mkdir -p "$stage/man"; sed "s/@VERSION@/$VERSION/" man/ais.1 > "$stage/man/ais.1"; }
    [ -f "$launcher" ]  && cp "$launcher"  "$stage/"

    lname=$(basename "$launcher")
    cat > "$stage/README.txt" <<EOF
AIS $VERSION  ($pretty/$arch) -- your memory, yours to keep.

GUI:   double-click  $lname     (opens the app in your browser)
CLI:   ./ais --help             (e.g.  ./ais venice italy ;  alias is='ais' for short)
New?   open USING.txt for a one-minute guide.

Your data is plain text you can find, back up, edit, or delete.
Run  ./ais --where  for its exact path (default: ~/.local/share/ais).
EOF
    if [ "$pretty" = macos ]; then
        cat >> "$stage/README.txt" <<EOF

macOS first run: the binaries are not yet notarized by Apple, so this downloaded
copy is quarantined and Gatekeeper says "Apple could not verify ais is free of
malware." Clear the flag once, from this folder in Terminal:
    xattr -dr com.apple.quarantine .
(or System Settings > Privacy & Security > Open Anyway). If ais still will not
run, the zip dropped its executable bit: chmod +x ais. For a graphical view run
'ais --serve', which opens the GUI in your browser.
EOF
    else
        cat >> "$stage/README.txt" <<EOF

Linux: if ais will not run, the zip dropped its executable bit: chmod +x ais.
Put it on your PATH (copy to ~/bin or /usr/local/bin) to use 'ais' anywhere; man
page in man/ais.1. For a graphical view run 'ais --serve', which opens the GUI
in your browser.
EOF
    fi

    # Deterministic perms now that every file exists: zip stores unix modes and
    # unzip restores them. Dirs 0755, data 0644, runnable files 0755.
    find "$stage" -type d -exec chmod 0755 {} +
    find "$stage" -type f -exec chmod 0644 {} +
    for x in ais "$lname"; do
        [ -f "$stage/$x" ] && chmod 0755 "$stage/$x"
    done

    if command -v zip >/dev/null 2>&1; then
        ( cd "$out" && rm -f "$name.zip" && zip -rq "$name.zip" "$name" ); pkg="$out/$name.zip"
    else
        tar -C "$out" -czf "$out/$name.tar.gz" "$name"; pkg="$out/$name.tar.gz"   # no zip: fall back
    fi
    rm -rf "$stage"; sidecar "$pkg"
    echo "built $pkg (+ .md5)"
}

case "${1:-all}" in
    src) build_src ;;
    bin) build_bin ;;
    *)   build_bin; build_src ;;
esac
