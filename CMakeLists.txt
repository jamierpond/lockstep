cmake_minimum_required(VERSION 3.31)

set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING
        "Oldest macOS lockstep runs on; eacp-core links UniformTypeIdentifiers, which is 11.0+")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(LOCKSTEP_LIBRARY_CACHE ON CACHE BOOL
        "Link each locked package's static libraries from lockstep's shared library cache when an earlier build compiled them from the same inputs, instead of compiling them again")
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)

project(lockstep VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No source this project compiles uses C++20 modules, so scanning each one for them is wasted work.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(EACP_BUILD_GRAPHICS OFF CACHE BOOL "" FORCE)
set(EACP_BUILD_WEBVIEW OFF CACHE BOOL "" FORCE)
set(EACP_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(EACP_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(EACP_BUILD_SPIRV OFF CACHE BOOL "" FORCE)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/linux-static-curl.cmake)
endif ()

lockstep_add_package(NAME eacp GITHUB eyalamirmusic/eacp
        GIT_TAG 57b2ef604bd12d340d7c62873da74525611e9071 EXCLUDE_FROM_ALL)
lockstep_add_package(NAME emberstore GITHUB tamber-inc/emberstore
        GIT_TAG 2cda8a019ff2df7ee135f1b5a89847378bbcaf55 EXCLUDE_FROM_ALL)

add_library(lockstep-lib STATIC
        src/BuiltWith.cpp
        src/ByteSize.cpp
        src/BuiltWithFields.cpp
        src/CMakeSnippet.cpp
        src/Commands.cpp
        src/CompileFlags.cpp
        src/CompilerExecutable.cpp
        src/ConfigureDescription.cpp
        src/ExecutableCache.cpp
        src/KeyText.cpp
        src/Messages.cpp
        src/Download.cpp
        src/Git.cpp
        src/LibraryCache.cpp
        src/LibraryCacheCommands.cpp
        src/LibraryCacheLocation.cpp
        src/LibraryCachePlan.cpp
        src/LibraryCacheSettings.cpp
        src/LibraryEviction.cpp
        src/LibraryStore.cpp
        src/LockFile.cpp
        src/LongPath.cpp
        src/MerkleHash.cpp
        src/PackageArguments.cpp
        src/PackageCache.cpp
        src/PinnedRelease.cpp
        src/ProjectSettings.cpp
        src/Session.cpp
        src/Status.cpp
        src/Submodules.cpp
        src/Sha256.cpp
        src/VersionRange.cpp)

target_include_directories(lockstep-lib PUBLIC src)
target_link_libraries(lockstep-lib PUBLIC eacp-core eacp-network)
target_link_libraries(lockstep-lib PRIVATE emberstore)

add_executable(lockstep src/Main.cpp)

if (WIN32)
    target_sources(lockstep PRIVATE packaging/lockstep.manifest)
endif ()
target_link_libraries(lockstep PRIVATE lockstep-lib)
# res_embed_add links ResEmbed into lockstep directly, ahead of eacp-core and
# Miro, which need it too; without reordering CMake repeats it after them and
# Apple's ld warns about the duplicate.
set_property(TARGET lockstep PROPERTY LINK_LIBRARIES_STRATEGY REORDER_FREELY)
res_embed_add(lockstep
        FILES cmake/lockstep-module.cmake cmake/lockstep-library-cache.cmake
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

install(TARGETS lockstep RUNTIME DESTINATION bin)
