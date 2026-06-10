#!/bin/sh
# dist.sh -- build release bundles into releases/<platform>/ (kept across runs).
#   make dist       this platform's BINARY bundle + the SOURCE bundle
#   make dist-src   just the source bundle (build anywhere, incl. Windows/Cygwin)
#
# Names (ripgrep/fd convention -- OS+arch = binary, -src = source):
#   ais-<ver>-src.zip                 source       (zip: universal / Windows-friendly)
#   ais-<ver>-<os>-<arch>.tar.gz      unix binary  (linux, macos)
#   ais-<ver>-windows-<arch>.zip      windows binary (when built; zip)
# Each bundle gets a same-named .md5 sidecar -- the md5 lives OUTSIDE the artifact
# (you verify the download against it).
#
# One machine can't cross-build the others -> run `make dist` on each. Windows is
# the SOURCE bundle built under Cygwin (a POSIX layer; the code compiles
# unchanged). A native Cygwin-free ais.exe would need a port -- not done.
set -e
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION=0.1
mkdir -p releases

# same-named .md5 next to the package file $1 (verify the download).
sidecar() {
    d=$(dirname "$1"); f=$(basename "$1")
    ( cd "$d" && { md5sum "$f" 2>/dev/null || md5 -r "$f"; } > "$f.md5" )
}

build_src() {
    name="ais-$VERSION-src"; out="releases/src"; stage="$out/$name"
    mkdir -p "$out"; rm -rf "$stage"; mkdir -p "$stage"
    for item in Makefile README.md COPYING performance.txt limitations.txt \
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
    os=$(uname -s | tr 'A-Z' 'a-z'); arch=$(uname -m); winbin=0
    # clean build so flags take (a stale c/ais would not relink as static).
    case "$os" in
        linux)  make -C c clean >/dev/null && make -C c static >/dev/null
                pretty=linux; launcher=gui/ais-web.desktop ;;
        darwin) make -C c clean >/dev/null && make -C c >/dev/null
                pretty=macos; launcher=gui/ais-web.command ;;
        cygwin*|msys*|mingw*)
                make -C c clean >/dev/null && make -C c >/dev/null
                pretty=windows; launcher=gui/ais-web.bat; winbin=1 ;;
        *) echo "dist: no binary for '$os'"; return 0 ;;
    esac
    name="ais-$VERSION-$pretty-$arch"; out="releases/$pretty"; stage="$out/$name"
    mkdir -p "$out"; rm -rf "$stage"; mkdir -p "$stage"

    if [ "$winbin" = 1 ]; then
        bin=c/ais.exe; [ -f "$bin" ] || bin=c/ais
        cp "$bin" "$stage/ais.exe"
        # a Cygwin-built exe needs the Cygwin runtime beside it, so it runs on
        # any Windows WITHOUT installing Cygwin. AIS links libc only -> cygwin1.dll.
        for dll in /usr/bin/cygwin1.dll /bin/cygwin1.dll; do
            [ -f "$dll" ] && { cp "$dll" "$stage/"; break; }
        done
    else
        cp c/ais "$stage/ais"
    fi
    [ -f COPYING ]      && cp COPYING      "$stage/"
    [ -f doc/about.txt ] && cp doc/about.txt "$stage/"
    [ -f man/ais.1 ]    && cp man/ais.1    "$stage/"
    [ -f "$launcher" ]  && cp "$launcher"  "$stage/"

    cat > "$stage/README.txt" <<EOF
AIS $VERSION  ($pretty/$arch)

Run the GUI:   ./ais --serve      (starts a local server, opens your browser)
Command line:  ./ais --help
Shortcut:      alias is='ais'   (bash/zsh) -- two-character recall: "is venice italy"

Your data is yours -- plain text you can find, back up, edit, or delete.
Run './ais --where' for its exact path (default: ~/.local/share/ais, under your home).
EOF
    if [ "$winbin" = 1 ]; then
        cat >> "$stage/README.txt" <<EOF

Windows: double-click ais-web.bat for the GUI, or run 'ais.exe serve' in a
terminal. Keep cygwin1.dll next to ais.exe (it is the runtime) -- no Cygwin
install is needed to RUN it.
EOF
    elif [ "$pretty" = macos ]; then
        cat >> "$stage/README.txt" <<EOF

macOS first run: the binary is unsigned, so right-click it (or the launcher)
and choose Open once to clear Gatekeeper.
EOF
    else
        cat >> "$stage/README.txt" <<EOF

Put it on your PATH to use 'ais' from any directory (copy to ~/bin or
/usr/local/bin).
EOF
    fi

    if [ "$winbin" = 1 ] && command -v zip >/dev/null 2>&1; then
        ( cd "$out" && rm -f "$name.zip" && zip -rq "$name.zip" "$name" ); pkg="$out/$name.zip"
    else
        tar -C "$out" -czf "$out/$name.tar.gz" "$name"; pkg="$out/$name.tar.gz"
    fi
    rm -rf "$stage"; sidecar "$pkg"
    echo "built $pkg (+ .md5)"
}

case "${1:-all}" in
    src) build_src ;;
    bin) build_bin ;;
    *)   build_bin; build_src ;;
esac
