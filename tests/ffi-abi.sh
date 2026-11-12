#!/bin/sh
# ffi-abi.sh -- the Dart binding must not fix the width of a C `long`.
#
# c/embed.h passes ids and counts as `long`, which is 32 bits on armeabi-v7a
# and on 64-bit Windows. app/flutter/lib/ais_ffi.dart bound them as Int64 from
# its first version; on a 32-bit ARM phone every argument after one shifted,
# the engine took the page size for the `to` date pointer, and build 627
# crashed in tl_in_range and store_record_at. No test ran on that ABI. The
# header has no 64-bit fixed-width type, so the binding needs none: `Long`.
#
# Exit 0 = pass, 1 = fail.

root=$(cd "$(dirname "$0")/.." && pwd)
dart="$root/app/flutter/lib/ais_ffi.dart"

if grep -nE '\b(Int64|Uint64)\b' "$dart"; then
    echo "  FAIL $dart binds a 64-bit fixed-width type; a C long is Long"
    exit 1
fi
if grep -qE '\b(u?int64_t|long long)\b' "$root/c/embed.h"; then
    echo "  FAIL c/embed.h gained a 64-bit fixed-width type; extend this check"
    exit 1
fi
echo "  ok   no fixed 64-bit type in the Dart binding"
