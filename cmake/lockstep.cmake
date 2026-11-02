# lockstep bootstrap: commit this file and load it before project() with
#   set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)
# It downloads the pinned lockstep binary and includes the CMake module it writes.
cmake_minimum_required(VERSION 3.25)

set(LOCKSTEP_VERSION 0.2.0)
set(LOCKSTEP_RELEASE_URL https://github.com/jamierpond/lockstep/releases/download)
set(LOCKSTEP_SHA256_macos-arm64 "f4f495a5fb1adc6aab9be264153bc99039f88c5755a88d279cc2d3c95d936f79")
set(LOCKSTEP_SHA256_macos-x64 "f4f495a5fb1adc6aab9be264153bc99039f88c5755a88d279cc2d3c95d936f79")
set(LOCKSTEP_SHA256_windows-x64 "397b1bd787e33f34055ca5027d4f22229d02cff4f8f9b354873c053ee1f35077")
set(LOCKSTEP_SHA256_windows-arm64 "f4fed0bfe7e45e2b6ee8bafda8c52070ed51edf75fb5cbb4c751c3631fafcfe3")
set(LOCKSTEP_SHA256_linux-x64 "3c515178a6559aaa1bebc5b0c5878faf3a0635fb6dbdb10d9a15219b4a767feb")
set(LOCKSTEP_SHA256_linux-arm64 "0c99d632c7869b3e6ebc1ff73f0d12aad608ba7bd3195f7760701f41a25cb10b")

set(LOCKSTEP_BOOTSTRAP "${CMAKE_CURRENT_LIST_FILE}")
set(LOCKSTEP_CACHE "$ENV{LOCKSTEP_CACHE}" CACHE PATH "The shared lockstep cache")
set(LOCKSTEP_EXECUTABLE "$ENV{LOCKSTEP_EXECUTABLE}" CACHE FILEPATH
        "A lockstep binary to run instead of downloading the pinned release")

if (NOT LOCKSTEP_EXECUTABLE)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}" host)
    string(REGEX REPLACE "^darwin" "macos" host "${host}")
    string(REGEX REPLACE "(x86_64|amd64)$" "x64" host "${host}")
    string(REGEX REPLACE "aarch64$" "arm64" host "${host}")
    set(cache "$ENV{HOME}/.cache/lockstep")
    if (LOCKSTEP_CACHE)
        set(cache "${LOCKSTEP_CACHE}")
    elseif (CMAKE_HOST_WIN32)
        set(cache "$ENV{LOCALAPPDATA}/lockstep")
    elseif (DEFINED ENV{XDG_CACHE_HOME})
        set(cache "$ENV{XDG_CACHE_HOME}/lockstep")
    endif ()
    set(asset "lockstep-${LOCKSTEP_VERSION}-${host}${CMAKE_HOST_EXECUTABLE_SUFFIX}")
    set(LOCKSTEP_EXECUTABLE "${cache}/bin/${LOCKSTEP_VERSION}/${asset}")
    if (NOT EXISTS "${LOCKSTEP_EXECUTABLE}")
        file(DOWNLOAD "${LOCKSTEP_RELEASE_URL}/v${LOCKSTEP_VERSION}/${asset}"
                "${LOCKSTEP_EXECUTABLE}.part" EXPECTED_HASH SHA256=${LOCKSTEP_SHA256_${host}})
        file(CHMOD "${LOCKSTEP_EXECUTABLE}.part" FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
        file(RENAME "${LOCKSTEP_EXECUTABLE}.part" "${LOCKSTEP_EXECUTABLE}")
    endif ()
endif ()

execute_process(COMMAND "${CMAKE_COMMAND}" -E env "LOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
        "${LOCKSTEP_EXECUTABLE}" cmake-module
        "--output=${CMAKE_BINARY_DIR}/_lockstep/lockstep-module.cmake" COMMAND_ERROR_IS_FATAL ANY)
include("${CMAKE_BINARY_DIR}/_lockstep/lockstep-module.cmake")
