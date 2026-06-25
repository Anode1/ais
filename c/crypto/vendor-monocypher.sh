#!/bin/sh
# Vendor Monocypher (the crypto primitives) into this directory, in source form.
#
# We pin an exact version and verify its SHA-512 before trusting it: supply-chain
# safety for a security module. Nothing is fetched at build time; this runs once,
# by hand, and the two .c/.h files are then committed alongside ais_crypto.c.
#
# Cross-channel verification (the bytes and the hash come from DIFFERENT hosts):
#   - the tarball is the AUTHOR-UPLOADED release asset on GitHub -- not the
#     auto-generated /archive/refs/tags/ tarball, whose bytes the author does not
#     vouch for and which GitHub can regenerate;
#   - the SHA-512 is the one published on monocypher.org:
#       https://monocypher.org/download/monocypher-<VER>.tar.gz.sha512
#     Read it there and paste it into EXPECTED_SHA512 below. Tarball from GitHub +
#     hash from monocypher.org means an attacker must subvert both to slip code in.
#   (Monocypher does not PGP-sign releases; SHA-512 / BLAKE2b on monocypher.org is
#    the strongest verification it offers.)
#
# Monocypher is dual-licensed CC0-1.0 / BSD-2-Clause (permissive, GPL-compatible),
# so it bundles cleanly into GPLv2 ais. Its LICENSE is vendored too (see README.md).
#
#   sh vendor-monocypher.sh
#
# Needs only core tools: curl (or wget), sha512sum (or shasum), tar.
set -eu

# pin the version here
VERSION="4.0.3"
URL="https://github.com/LoupVaillant/Monocypher/releases/download/${VERSION}/monocypher-${VERSION}.tar.gz"

# Paste the SHA-512 from
#   https://monocypher.org/download/monocypher-${VERSION}.tar.gz.sha512
# Fail-closed: the script refuses to vendor until this is set.
EXPECTED_SHA512="40904ada5c7ee4f7741733e38b69a30a4b0561cbffba5ffe7c2dce16136d540251ec0d9056ff606510d3b5b708fb8a40db7e0870d4a0b2dc17ba2bfb880f8965"

dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# fetch the pinned tarball (the author-uploaded release asset)
if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$URL" -o "$tmp/m.tgz"
elif command -v wget >/dev/null 2>&1; then
    wget -qO "$tmp/m.tgz" "$URL"
else
    echo "need curl or wget" >&2; exit 1
fi

# SHA-512 (fail-closed: refuse to vendor an unverified archive)
if command -v sha512sum >/dev/null 2>&1; then
    got="$(sha512sum "$tmp/m.tgz" | cut -d' ' -f1)"
else
    got="$(shasum -a 512 "$tmp/m.tgz" | cut -d' ' -f1)"
fi
echo "downloaded Monocypher ${VERSION}, sha512=${got}"
if [ -z "$EXPECTED_SHA512" ]; then
    echo "EXPECTED_SHA512 is empty: set it in this script to the value from" >&2
    echo "  https://monocypher.org/download/monocypher-${VERSION}.tar.gz.sha512" >&2
    echo "then re-run." >&2
    exit 1
fi
if [ "$got" != "$EXPECTED_SHA512" ]; then
    echo "checksum MISMATCH -- refusing to vendor. expected ${EXPECTED_SHA512}" >&2
    exit 1
fi

# Extract and locate the CORE source (robust to the tarball's top-dir name).
# Only the core monocypher.{c,h} are needed: ais_crypto uses Argon2 + the
# XChaCha20-Poly1305 AEAD, not the optional Ed25519 module (its own files).
tar -xzf "$tmp/m.tgz" -C "$tmp"
mc_c="$(find "$tmp" -type f -name monocypher.c | head -1)"
mc_h="$(find "$tmp" -type f -name monocypher.h | head -1)"
lic="$(find "$tmp" -type f -iname 'LICENSE*' | head -1)"
if [ -z "$mc_c" ] || [ -z "$mc_h" ]; then
    echo "monocypher.c / monocypher.h not found in the tarball" >&2
    exit 1
fi

cp "$mc_c" "$dir/monocypher.c"
cp "$mc_h" "$dir/monocypher.h"
[ -n "$lic" ] && cp "$lic" "$dir/LICENSE.monocypher"

echo "vendored: monocypher.c monocypher.h LICENSE.monocypher (v${VERSION})"
echo "now run: make   (from the repo root or c/)"
