# Builds the AIS C engine (../../../c) as the shared library the Dart FFI loads
# (libais.so). Include this from the platform native builds:
#   * Android: android/app/build.gradle -> externalNativeBuild { cmake { path "../../src/CMakeLists.txt" } }
#   * Linux:   linux/CMakeLists.txt      -> add_subdirectory(../src ais_build)
# iOS/macOS do not use this -- there the C sources are compiled straight into
# the app target (see README), and the FFI uses DynamicLibrary.process().
cmake_minimum_required(VERSION 3.12)   # >= 3.12 for CONFIGURE_DEPENDS on the glob below
project(ais C)
set(CMAKE_C_STANDARD 99)

# Engine = every c/*.c AND c/crypto/*.c EXCEPT the two that carry a main()
# (see the filter below). crypto/ holds ais_crypto.c + the vendored monocypher.c; secret.c
# calls them (the header resolves via the -I below), so they MUST be compiled
# in -- otherwise libais.so ships an unresolved aisc_encrypt and the app fails
# to load on Android. The C Makefile auto-globs subdirs; this must match it.
# CONFIGURE_DEPENDS re-checks the glob on every build, so a newly added c/*.c
# (e.g. sync.c) lands in libais.so on an INCREMENTAL build too; without it a
# stale CMake cache omits the new file and the APK fails to link (undefined
# symbol sync_serve/sync_pull) until a manual `flutter clean`.
file(GLOB AIS_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../../c/*.c"
                  "${CMAKE_CURRENT_SOURCE_DIR}/../../../c/crypto/*.c")
# main.c is the CLI's entry point and tests.c is the test suite's. Both carry a
# main(), and neither belongs in a library. tests.c had been matching the glob
# and compiling on every build (its .o sat in .cxx/); the shipped .so carried
# none of it, since the link drops code nothing references, so this costs build
# time rather than APK size. iOS is where it would have mattered: there these
# sources link into an EXECUTABLE that already has a main() of its own.
list(FILTER AIS_SRC EXCLUDE REGEX "/(main|tests)\\.c$")

# Target is 'ais_engine' (the Linux runner executable target is already named
# 'ais'); OUTPUT_NAME keeps the file libais.so, which the Dart FFI opens.
add_library(ais_engine SHARED ${AIS_SRC})
set_target_properties(ais_engine PROPERTIES OUTPUT_NAME ais C_VISIBILITY_PRESET default)
target_include_directories(ais_engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../c")

# Stamp the version, exactly as c/Makefile does for the CLI. Without this the
# library falls back to the "0.0.0-dev" default in ais.h, so ais_version() -- the
# whole point of which is to reveal WHICH engine an app is running -- reports
# nothing useful, and the About screen that exists to catch a stale engine cannot.
# A source copy with no .git keeps the fallback, which is visibly wrong rather
# than silently wrong. See doc/dev/VERSIONING.md.
# A RELEASE is built at a tag, so ask for the tag first. `--dirty` is right for a
# dev build but wrong here: CI's own build steps touch tracked files, so the
# published v0.3.11 artifact reported "0.3.11-dirty" -- which reads as "built from
# uncommitted changes" and would send anyone debugging it down the wrong path.
# Off a tag, fall back to the descriptive form, dirty marker included.
execute_process(
    COMMAND git describe --exact-match --tags HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE AIS_GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT AIS_GIT_VERSION)
    execute_process(
        COMMAND git describe --tags --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE AIS_GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if(AIS_GIT_VERSION)
    string(REGEX REPLACE "^v" "" AIS_GIT_VERSION "${AIS_GIT_VERSION}")
    target_compile_definitions(ais_engine PRIVATE AIS_VERSION="${AIS_GIT_VERSION}")
    message(STATUS "ais engine version: ${AIS_GIT_VERSION}")
endif()

# execute_process runs at CONFIGURE time, so without this the version is frozen
# at whatever it was when the build directory was first created: an incremental
# release build after a commit or a tag keeps stamping the OLD string, and a
# release artifact that misreports its own version is worse than one that has
# none. Re-configure when HEAD, the ref it points at, or the index changes.
# (The CLI's Makefile has no such problem -- $(shell) re-runs every invocation.)
set(AIS_GIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../.git")
if(EXISTS "${AIS_GIT_DIR}/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${AIS_GIT_DIR}/HEAD" "${AIS_GIT_DIR}/index")
endif()

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
