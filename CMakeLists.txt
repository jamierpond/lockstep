cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING
        "Oldest macOS lockstep runs on; eacp-core links UniformTypeIdentifiers, which is 11.0+")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)

project(lockstep VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EACP_BUILD_GRAPHICS OFF CACHE BOOL "" FORCE)
set(EACP_BUILD_WEBVIEW OFF CACHE BOOL "" FORCE)
set(EACP_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(EACP_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(EACP_BUILD_SPIRV OFF CACHE BOOL "" FORCE)

lockstep_add_package(NAME eacp GITHUB eyalamirmusic/eacp
        GIT_TAG 57b2ef604bd12d340d7c62873da74525611e9071 EXCLUDE_FROM_ALL)

add_library(lockstep-lib STATIC
        src/CMakeSnippet.cpp
        src/Commands.cpp
        src/Messages.cpp
        src/Download.cpp
        src/Git.cpp
        src/LockFile.cpp
        src/MerkleHash.cpp
        src/PackageArguments.cpp
        src/PackageCache.cpp
        src/PinnedRelease.cpp
        src/Session.cpp
        src/Submodules.cpp
        src/Sha256.cpp)

target_include_directories(lockstep-lib PUBLIC src)
target_link_libraries(lockstep-lib PUBLIC eacp-core eacp-network)

add_executable(lockstep src/Main.cpp)
target_link_libraries(lockstep PRIVATE lockstep-lib)
# res_embed_add links ResEmbed into lockstep directly, ahead of eacp-core and
# Miro, which need it too; without reordering CMake repeats it after them and
# Apple's ld warns about the duplicate.
set_property(TARGET lockstep PROPERTY LINK_LIBRARIES_STRATEGY REORDER_FREELY)
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

install(TARGETS lockstep RUNTIME DESTINATION bin)
