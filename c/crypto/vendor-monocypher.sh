#!/bin/sh
# Vendor Monocypher (the crypto primitives) into this directory, in source form.
#
# We pin an exact version and verify its checksum before trusting it: supply-chain
# safety for a security module. Nothing is fetched at build time; this runs once,
# by hand, and the two .c/.h files are then committed alongside ais_crypto.c.
#
# Monocypher is dual-licensed CC0-1.0 / BSD-2-Clause (permissive, GPL-compatible),
# so it bundles cleanly into GPLv2 ais. Its LICENSE is vendored too (see README.md).
#
#   sh vendor-monocypher.sh
#
# Needs only core tools: curl (or wget), sha256sum (or shasum), tar.
set -eu

# pin the version here
VERSION="4.0.2"
URL="https://github.com/LoupVaillant/Monocypher/archive/refs/tags/${VERSION}.tar.gz"

# Verify this against the official release before trusting the download. Get it
# from monocypher.org / the GitHub release page, then paste it here (fail-closed).
EXPECTED_SHA256=""

dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# fetch the pinned tarball
if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$URL" -o "$tmp/m.tgz"
elif command -v wget >/dev/null 2>&1; then
    wget -qO "$tmp/m.tgz" "$URL"
else
    echo "need curl or wget" >&2; exit 1
fi

# checksum (fail-closed: refuse to vendor an unverified archive)
if command -v sha256sum >/dev/null 2>&1; then
    got="$(sha256sum "$tmp/m.tgz" | cut -d' ' -f1)"
else
    got="$(shasum -a 256 "$tmp/m.tgz" | cut -d' ' -f1)"
fi
echo "downloaded Monocypher ${VERSION}, sha256=${got}"
if [ -z "$EXPECTED_SHA256" ]; then
    echo "EXPECTED_SHA256 is empty: set it in this script to the official hash above, then re-run." >&2
    exit 1
fi
if [ "$got" != "$EXPECTED_SHA256" ]; then
    echo "checksum MISMATCH -- refusing to vendor. expected ${EXPECTED_SHA256}" >&2
    exit 1
fi

tar -xzf "$tmp/m.tgz" -C "$tmp"
src="$tmp/Monocypher-${VERSION}"

cp "$src/src/monocypher.c" "$dir/monocypher.c"
cp "$src/src/monocypher.h" "$dir/monocypher.h"
cp "$src/LICENSE.md"        "$dir/LICENSE.monocypher"

echo "vendored: monocypher.c monocypher.h LICENSE.monocypher (v${VERSION})"
echo "now run: make   (from the repo root or c/)"
