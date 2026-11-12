#!/bin/sh
# ffi-abi.sh -- names the code must not use, each one a defect that shipped.
#
# Every rule is a grep, so it runs with no device and no toolchain. A rule is
# added when a bug of its class is found, with the build it broke.
#
# 1. The Dart binding fixes no integer at 64 bits. c/embed.h passes ids and
#    counts as `long`, 32 bits on armeabi-v7a and on 64-bit Windows; bound as
#    64 bits, every argument after one shifted, the engine took the page size
#    for the `to` date pointer, and build 627 crashed on 32-bit ARM phones.
# 2. c/embed.h has no 64-bit fixed-width type, which is why rule 1 can be
#    absolute. If it gains one, rule 1 needs an exception list.
# 3. The app decodes an engine string with engineText, never the strict
#    toDartString: the engine stores any bytes, and one record with a
#    Windows-codepage byte threw for the whole page. The one strict decode
#    left is docRead, which refuses a non-text document on purpose.
# 4. Every symbol the binding looks up is declared in c/embed.h or c/ais.h.
# 5. A CLI operand is not parsed with atol: `--add abc` became record 0.
#    main.c goes through id_operand.
# 6. A cast to time_t sits in a file that tests sizeof(time_t): it is 32 bits
#    on armeabi-v7a, where an import date past 2038 came out as 1963.
#
# Exit 0 = pass, 1 = fail.

root=$(cd "$(dirname "$0")/.." && pwd)
lib="$root/app/flutter/lib"
dart="$lib/ais_ffi.dart"
rc=0
bad() { echo "  FAIL $1"; rc=1; }

if grep -rnE '\b(Int64|Uint64)\b' "$lib"; then
    bad "rule 1: a fixed 64-bit type in the app; a C long is Long"
fi
if grep -nE '\b(u?int64_t|long long)\b' "$root/c/embed.h"; then
    bad "rule 2: c/embed.h gained a 64-bit fixed-width type; extend rule 1"
fi
n=$(grep -rn 'toDartString' "$lib" | grep -vc '^[^:]*:[0-9]*: *//')
if [ "$n" -ne 1 ]; then
    grep -rn 'toDartString' "$lib"
    bad "rule 3: $n strict decodes in the app, expected 1 (docRead); use engineText"
fi
for sym in $(grep -o "'ais_[a-z_]*'" "$dart" | tr -d "'" | sort -u); do
    grep -qE "\b$sym\(" "$root/c/embed.h" "$root/c/ais.h" ||
        bad "rule 4: the binding looks up $sym, which no header declares"
done
if grep -nE '\batol\(' "$root/c/main.c"; then
    bad "rule 5: atol in c/main.c; an ID operand goes through id_operand"
fi
for f in $(grep -lE '\(time_t\)' "$root"/c/*.c | grep -v '/tests\.c$'); do
    grep -q 'sizeof(time_t)' "$f" ||
        bad "rule 6: $f casts to time_t without testing sizeof(time_t)"
done

[ $rc -eq 0 ] && echo "  ok   6 rules hold"
exit $rc
