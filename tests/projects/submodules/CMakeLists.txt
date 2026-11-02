cmake_minimum_required(VERSION 3.24)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${LOCKSTEP_SHIM})
project(SubmodulesSample LANGUAGES CXX)

# libsodium-cmake carries libsodium itself as a git submodule.
lockstep_add_package(NAME libsodium-cmake GITHUB robinlinden/libsodium-cmake
                     GIT_TAG cfebfd3da486d5a86c644c8b47067e5411c7599c DOWNLOAD_ONLY)

if (NOT EXISTS "${libsodium-cmake_SOURCE_DIR}/libsodium/src/libsodium/include/sodium/version.h.in")
    message(FATAL_ERROR "libsodium-cmake's libsodium submodule was not checked out")
endif ()

add_executable(sample main.cpp)
