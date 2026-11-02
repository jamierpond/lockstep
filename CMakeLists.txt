cmake_minimum_required(VERSION 3.24)

set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING
        "Oldest macOS lockstep runs on; eacp-core links UniformTypeIdentifiers, which is 11.0+")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(lockstep VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/CPM.cmake)

set(EACP_BUILD_GRAPHICS OFF CACHE BOOL "" FORCE)
set(EACP_BUILD_WEBVIEW OFF CACHE BOOL "" FORCE)
set(EACP_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(EACP_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)

CPMAddPackage(
        NAME eacp
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG 57b2ef604bd12d340d7c62873da74525611e9071
        EXCLUDE_FROM_ALL YES)

add_library(lockstep-lib STATIC
        src/CMakeSnippet.cpp
        src/Commands.cpp
        src/Download.cpp
        src/Git.cpp
        src/LockFile.cpp
        src/MerkleHash.cpp
        src/PackageArguments.cpp
        src/PackageCache.cpp
        src/PinnedRelease.cpp
        src/Session.cpp
        src/Sha256.cpp)

target_include_directories(lockstep-lib PUBLIC src)
target_link_libraries(lockstep-lib PUBLIC eacp-core eacp-network)

add_executable(lockstep src/Main.cpp)
target_link_libraries(lockstep PRIVATE lockstep-lib)
res_embed_add(lockstep
        FILES cmake/lockstep-module.cmake
        NAMESPACE LockstepModule
        CATEGORY LockstepModule)
target_compile_definitions(lockstep PRIVATE
        LOCKSTEP_VERSION="${PROJECT_VERSION}")

option(LOCKSTEP_FULLY_STATIC "Link lockstep with -static (for a musl build on Linux)" OFF)

if (LOCKSTEP_FULLY_STATIC)
    target_link_options(lockstep PRIVATE -static)
endif ()

option(LOCKSTEP_ENABLE_TESTS "Build lockstep's tests" ${PROJECT_IS_TOP_LEVEL})

if (LOCKSTEP_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()
