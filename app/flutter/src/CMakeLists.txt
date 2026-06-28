# Builds the AIS C engine (../../../c) as the shared library the Dart FFI loads
# (libais.so). Include this from the platform native builds:
#   * Android: android/app/build.gradle -> externalNativeBuild { cmake { path "../../src/CMakeLists.txt" } }
#   * Linux:   linux/CMakeLists.txt      -> add_subdirectory(../src ais_build)
# iOS/macOS do not use this -- there the C sources are compiled straight into
# the app target (see README), and the FFI uses DynamicLibrary.process().
cmake_minimum_required(VERSION 3.10)
project(ais C)
set(CMAKE_C_STANDARD 99)

# Engine = every c/*.c AND c/crypto/*.c EXCEPT main.c (a library carries no
# main()). crypto/ holds ais_crypto.c + the vendored monocypher.c; secret.c
# calls them (the header resolves via the -I below), so they MUST be compiled
# in -- otherwise libais.so ships an unresolved aisc_encrypt and the app fails
# to load on Android. The C Makefile auto-globs subdirs; this must match it.
file(GLOB AIS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../c/*.c"
                  "${CMAKE_CURRENT_SOURCE_DIR}/../../../c/crypto/*.c")
list(FILTER AIS_SRC EXCLUDE REGEX "/main\\.c$")

# Target is 'ais_engine' (the Linux runner executable target is already named
# 'ais'); OUTPUT_NAME keeps the file libais.so, which the Dart FFI opens.
add_library(ais_engine SHARED ${AIS_SRC})
set_target_properties(ais_engine PROPERTIES OUTPUT_NAME ais C_VISIBILITY_PRESET default)
target_include_directories(ais_engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../c")

# Single source of truth: assert libais.so has NO unresolved crypto symbols, so
# the regression "c/crypto/*.c dropped from the glob -> undefined aisc_encrypt ->
# crash on first encrypt/reveal" is caught HERE, at the one place that builds the
# library -- for Android (NDK), Linux desktop, and any future consumer alike, so
# the CI need not re-check it per platform (and cannot drift). Uses the
# toolchain's nm (CMAKE_NM; llvm-nm under the NDK), reading the dynamic symbols.
if(CMAKE_NM)
    set(AISC_NM "${CMAKE_NM}")
else()
    find_program(AISC_NM NAMES llvm-nm nm)
endif()
if(AISC_NM)
    add_custom_command(TARGET ais_engine POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DNM=${AISC_NM}" "-DLIB=$<TARGET_FILE:ais_engine>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/assert_no_undefined_crypto.cmake"
        VERBATIM
        COMMENT "ais: checking libais.so for unresolved crypto symbols")
else()
    message(WARNING "ais: no nm/llvm-nm found; skipping the unresolved-crypto-symbol check")
endif()
