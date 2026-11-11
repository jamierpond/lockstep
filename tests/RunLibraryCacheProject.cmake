# Builds a project whose package has a static library with LOCKSTEP_LIBRARY_CACHE
# on, then builds it again in a fresh build directory, which must link the
# stored library without compiling the package's sources. The package enables C,
# which the project does not, and its key must cover the C compiler. Changing a compile
# definition on the library, a global flag or an option the package reads must
# miss instead. The environment variable LOCKSTEP_LIBRARY_CACHE alone turns the
# cache on, and -DLOCKSTEP_LIBRARY_CACHE=OFF beats it. `lockstep cache stats` and
# `lockstep cache clear` then run from inside a project that pins an older
# lockstep, which they must not hand over to. A Debug build skips greeter when MSVC
# writes its debug information to a separate .pdb, and caches it everywhere with
# CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded. Expects PROJECT_DIR, WORK_DIR,
# LOCKSTEP_EXECUTABLE, LOCKSTEP_SHIM, GENERATOR and MSVC_ABI (true when the
# compiler targets the MSVC ABI).

include("${CMAKE_CURRENT_LIST_DIR}/LibraryCacheProject.cmake")
create_greeter_fixture()

configure(build-first "0 hits, 1 misses, 0 skipped" -DLOCKSTEP_MODE=update)
build_and_run(build-first)
# The sample enables only CXX; greeter's own project() enables C, so its key must
# read the C compiler from greeter's directory, and the record of what built it
# the C compiler's version.
file(GLOB key_inputs "${WORK_DIR}/build-first/_lockstep/library-cache/inputs/*.txt")
file(READ "${key_inputs}" key_input_text)
if (NOT key_input_text MATCHES "CMAKE_C_COMPILER_ID=[^\n]")
    message(FATAL_ERROR "greeter's key does not cover CMAKE_C_COMPILER_ID:\n${key_input_text}")
endif ()
file(GLOB built_with "${WORK_DIR}/build-first/_lockstep/library-cache/inputs/*.built-with.json")
file(READ "${built_with}" built_with_text)
string(JSON compiler_count LENGTH "${built_with_text}" compilers)
string(JSON first_language GET "${built_with_text}" compilers 0 language)
string(JSON first_version GET "${built_with_text}" compilers 0 version)
if (NOT compiler_count EQUAL 2 OR NOT first_language STREQUAL "C" OR first_version STREQUAL "")
    message(FATAL_ERROR "greeter's record does not hold its C compiler's version:\n${built_with_text}")
endif ()
file(GLOB stored "${WORK_DIR}/cache/lib/format-*/*/*/manifest.json")
list(LENGTH stored stored_count)
if (NOT stored_count EQUAL 1)
    message(FATAL_ERROR "linking sample stored ${stored_count} libraries, not 1")
endif ()

configure(build-second "1 hits, 0 misses, 0 skipped")
build_and_run(build-second)
file(GLOB_RECURSE compiled "${WORK_DIR}/build-second/*greeter.cpp.o*")
if (compiled)
    message(FATAL_ERROR "the cache hit still compiled greeter.cpp: ${compiled}")
endif ()

configure(build-definition "0 hits, 1 misses, 0 skipped" -DSAMPLE_GREETER_EXTRA=1)
configure(build-global-flag "0 hits, 1 misses, 0 skipped" -DCMAKE_CXX_FLAGS=-DSAMPLE_FLAG)
configure(build-option "0 hits, 1 misses, 0 skipped" -DGREETER_LOUD=ON)

run_configure(build-environment LOCKSTEP_LIBRARY_CACHE=ON)
expect_summary(build-environment "1 hits, 0 misses, 0 skipped")
build_and_run(build-environment)

run_configure(build-environment-off LOCKSTEP_LIBRARY_CACHE=ON -DLOCKSTEP_LIBRARY_CACHE=OFF)
if (step_output MATCHES "lockstep: library cache:")
    message(FATAL_ERROR "-DLOCKSTEP_LIBRARY_CACHE=OFF did not turn off the cache the environment turned on")
endif ()

# A project pinning a release that cannot be fetched: handing over to it fails.
file(WRITE "${WORK_DIR}/pinned-project/lockstep.lock"
        "{\"bootstrap\": \"cmake/lockstep.cmake\", \"packages\": [], \"root\": \"\", \"version\": 1}\n")
file(WRITE "${WORK_DIR}/pinned-project/cmake/lockstep.cmake"
        "set(LOCKSTEP_VERSION 0.0.1)\nset(LOCKSTEP_RELEASE_URL file:///lockstep-no-such-release)\n")

# Of the four misses only build-first was built, so its link stored the one
# library and the other three were never stored.
run_cache_command(stats)
expect_output("libraries +1 ")
expect_output("hits +2 ")
expect_output("misses +4 ")
expect_output("  stored +1 ")
expect_output("not stored +3 ")

run_cache_command(clear)
run_cache_command(stats)
expect_output("libraries +0 .*hits +0 .*misses +0 .*stored +0 .*not stored +0 ")
if (NOT IS_DIRECTORY "${WORK_DIR}/cache/src")
    message(FATAL_ERROR "lockstep cache clear deleted <cache>/src")
endif ()

# CMake compiles a Debug build with MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase
# (/Zi) by default, whose .pdb the cache does not store; other compilers ignore it.
if (MSVC_ABI)
    configure(build-debug "0 hits, 0 misses, 1 skipped" -DCMAKE_BUILD_TYPE=Debug --log-level=VERBOSE)
    expect_output("library cache skips greeter: MSVC writes its debug information to a separate \\.pdb")
else ()
    configure(build-debug "0 hits, 1 misses, 0 skipped" -DCMAKE_BUILD_TYPE=Debug)
endif ()
build_and_run(build-debug)

configure(build-debug-embedded "0 hits, 1 misses, 0 skipped"
        -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded)
build_and_run(build-debug-embedded)
configure(build-debug-embedded-again "1 hits, 0 misses, 0 skipped"
        -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded)
build_and_run(build-debug-embedded-again)
