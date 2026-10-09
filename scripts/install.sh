#!/bin/sh
# install.sh -- put the current ais release on this machine.
#
#   curl -fsSL https://raw.githubusercontent.com/Anode1/ais/main/scripts/install.sh | sh
#
# Downloads the release built by GitHub Actions for this OS and CPU, checks it
# against the .sha256 published beside it, and installs the binary and its man
# page under a prefix you own. Nothing is compiled, nothing needs root, and the
# only thing that lands outside the prefix is nothing.
#
#   PREFIX=/usr/local sh install.sh    install there instead (needs write access)
#   VERSION=v0.3.27   sh install.sh    a specific release rather than the latest
#
# POSIX sh, so it runs unchanged on Linux and macOS. Read it before you pipe it
# to a shell: that is true of every install script, this one included.
set -e

REPO=Anode1/ais
PREFIX=${PREFIX:-$HOME/.local}
VERSION=${VERSION:-}

say()  { printf '%s\n' "$*"; }
die()  { printf 'install: %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# ---- what to fetch --------------------------------------------------------
os=$(uname -s)
arch=$(uname -m)
case $os in
    Linux)  os=linux ;;
    Darwin) os=macos ;;
    *) die "no published build for $os. Build from source instead:
    git clone https://github.com/$REPO && cd ais && make && sudo make install" ;;
esac
case $arch in
    x86_64|amd64)  arch=x86_64 ;;
    arm64|aarch64) arch=arm64 ;;
    *) die "no published build for $arch (build from source: make && sudo make install)" ;;
esac
# macOS ships one arm64 build. Rosetta 2 runs x86_64 code on Apple silicon, not
# the reverse, so an Intel Mac builds from source.
[ "$os" = macos ] && [ "$arch" = x86_64 ] && die "no published build for an Intel Mac. Build from source:
    git clone https://github.com/$REPO && cd ais && make && sudo make install"

have unzip || die "unzip is needed and was not found"
if have curl;   then get() { curl -fsSL "$1" -o "$2"; }; fetch() { curl -fsSL "$1"; }
elif have wget; then get() { wget -qO "$2" "$1"; };      fetch() { wget -qO- "$1"; }
else die "curl or wget is needed and neither was found"
fi

if [ -z "$VERSION" ]; then
    # The tag is part of every asset name, so it has to be resolved first.
    VERSION=$(fetch "https://api.github.com/repos/$REPO/releases/latest" |
              sed -n 's/.*"tag_name"[ ]*:[ ]*"\([^"]*\)".*/\1/p' | head -n1)
    [ -n "$VERSION" ] || die "cannot find the latest release (set VERSION=vX.Y.Z to pin one)"
fi

name="ais-$VERSION-$os-$arch"
url="https://github.com/$REPO/releases/download/$VERSION/$name.zip"
say "ais $VERSION for $os-$arch"

# ---- fetch, check, unpack -------------------------------------------------
tmp=$(mktemp -d "${TMPDIR:-/tmp}/ais-install.XXXXXX") || die "cannot make a temp directory"
trap 'rm -rf "$tmp"' EXIT INT TERM

get "$url" "$tmp/$name.zip" || die "download failed: $url"
if get "$url.sha256" "$tmp/$name.zip.sha256" 2>/dev/null; then
    if   have shasum;    then sum="shasum -a 256 -c"
    elif have sha256sum; then sum="sha256sum -c"
    else sum=""
    fi
    if [ -n "$sum" ]; then
        ( cd "$tmp" && $sum "$name.zip.sha256" >/dev/null ) ||
            die "checksum mismatch: the download does not match its published hash"
        say "checksum ok"
    else
        say "no shasum or sha256sum here: skipping the checksum"
    fi
else
    say "no published checksum for this release: skipping the check"
fi

unzip -q "$tmp/$name.zip" -d "$tmp" || die "cannot unpack $name.zip"
bin=$(find "$tmp" -type f -name ais | head -n1)
[ -n "$bin" ] || die "no ais binary inside $name.zip"

# ---- install --------------------------------------------------------------
mkdir -p "$PREFIX/bin" || die "cannot create $PREFIX/bin"
[ -w "$PREFIX/bin" ] || die "$PREFIX/bin is not writable (try PREFIX=\$HOME/.local, or run with sudo)"
cp "$bin" "$PREFIX/bin/ais.new" && chmod 755 "$PREFIX/bin/ais.new"
mv "$PREFIX/bin/ais.new" "$PREFIX/bin/ais"      # replace in one step, never half-written

man=$(find "$tmp" -type f -name ais.1 | head -n1)
if [ -n "$man" ] && mkdir -p "$PREFIX/share/man/man1" 2>/dev/null; then
    cp "$man" "$PREFIX/share/man/man1/ais.1" 2>/dev/null || true
fi

say "installed $PREFIX/bin/ais ($("$PREFIX/bin/ais" --version))"

# ---- what to do next ------------------------------------------------------
case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) say ""
       say "$PREFIX/bin is not on your PATH. Add this to your shell profile:"
       say "    export PATH=\"$PREFIX/bin:\$PATH\"" ;;
esac
cat <<'EOF'

Start here:
    ais -v https://example.org/page venice italy   save something under your keys
    ais venice italy                               get it back by those keys
    ais --serve                                    the same index in a browser

Give an agent the same index (read-only; add rw to allow saving):
    claude mcp add ais -- ais --mcp
EOF
