# assert_no_undefined_crypto.cmake -- fail the build if LIB (libais.so) has any
# UNDEFINED aisc_* symbol. That is the signature of c/crypto/*.c being dropped
# from the engine glob: the library still links (a shared lib tolerates undefined
# symbols), but ships an unresolved aisc_encrypt and crashes on the first
# encrypt/reveal on EVERY consumer (Android NDK, Linux desktop, future).
#
# Run from the POST_BUILD step in CMakeLists.txt:
#   cmake -DNM=<nm-or-llvm-nm> -DLIB=<libais.so> -P assert_no_undefined_crypto.cmake
#
# Reads the dynamic symbol table (-D), undefined entries only (-u); these survive
# the release strip. A correct lib still has many undefined libc symbols, so we
# only fail on aisc_* -- the crypto the engine itself must provide.

execute_process(
    COMMAND "${NM}" -D -u "${LIB}"
    OUTPUT_VARIABLE undef
    ERROR_QUIET)

string(REGEX MATCH "aisc_[A-Za-z0-9_]+" hit "${undef}")
if(hit)
    message(FATAL_ERROR
        "libais.so has an UNRESOLVED crypto symbol (${hit}). "
        "app/flutter/src/CMakeLists.txt must glob c/crypto/*.c into the engine, "
        "else encryption crashes on load.")
endif()

message(STATUS "ais: libais.so crypto symbols resolved (no undefined aisc_*)")
