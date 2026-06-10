# Builds the AIS C engine (../../../c) as the shared library the Dart FFI loads
# (libais.so). Include this from the platform native builds:
#   * Android: android/app/build.gradle -> externalNativeBuild { cmake { path "../../src/CMakeLists.txt" } }
#   * Linux:   linux/CMakeLists.txt      -> add_subdirectory(../src ais_build)
# iOS/macOS do not use this -- there the C sources are compiled straight into
# the app target (see README), and the FFI uses DynamicLibrary.process().
cmake_minimum_required(VERSION 3.10)
project(ais C)
set(CMAKE_C_STANDARD 99)

# Engine = every c/*.c EXCEPT main.c (a library carries no main()).
file(GLOB AIS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../c/*.c")
list(FILTER AIS_SRC EXCLUDE REGEX "/main\\.c$")

# Target is 'ais_engine' (the Linux runner executable target is already named
# 'ais'); OUTPUT_NAME keeps the file libais.so, which the Dart FFI opens.
add_library(ais_engine SHARED ${AIS_SRC})
set_target_properties(ais_engine PROPERTIES OUTPUT_NAME ais C_VISIBILITY_PRESET default)
target_include_directories(ais_engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../c")
