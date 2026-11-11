# Turns the library cache on and sets a compiler range from lockstep.json beside
# the lock, with no -D and no environment variable: the cache is on and the range
# admits a build that claims another compiler version. The environment variable
# beats the file and a -D beats both. A key lockstep.json does not know stops
# the configure, naming the key and the file, and so does a size that is not one. `lockstep cache stats` run in a
# project shows each setting and the file it came from. Expects PROJECT_DIR,
# WORK_DIR, LOCKSTEP_EXECUTABLE, LOCKSTEP_SHIM and GENERATOR.

include("${CMAKE_CURRENT_LIST_DIR}/LibraryCacheProject.cmake")

function(expect_text text)
    string(REGEX REPLACE "[ \n]+" " " output "${step_output}")
    string(REGEX REPLACE "[ \n]+" " " text "${text}")
    string(FIND "${output}" "${text}" position)
    if (position EQUAL -1)
        message(FATAL_ERROR "expected output holding '${text}'")
    endif ()
endfunction()

# Configures with ARGN and expects the configure to fail with `text` in its output.
function(expect_configure_to_stop build text)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=LOCKSTEP_LIBRARY_CACHE
            "${CMAKE_COMMAND}" -S "${PROJECT_DIR}" -B "${WORK_DIR}/${build}" -G "${GENERATOR}"
            "-DLOCKSTEP_SHIM=${LOCKSTEP_SHIM}" "-DLOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
            "-DLOCKSTEP_CACHE=${WORK_DIR}/cache" "-DLOCKSTEP_LOCK_FILE=${WORK_DIR}/lockstep.lock"
            "-DFIXTURE_URL=${fixture_url}" ${ARGN}
            RESULT_VARIABLE exit_code OUTPUT_VARIABLE step_output ERROR_VARIABLE step_output)
    if (exit_code EQUAL 0)
        message(FATAL_ERROR "${build} configured, but should have stopped with '${text}'")
    endif ()
    expect_text("${text}")
endfunction()

create_greeter_fixture()
set(settings_file "${WORK_DIR}/lockstep.json")
file(WRITE "${settings_file}" [=[{"libraryCache": {"enabled": true, "compilerRange": ">=21"}}]=])

run_configure(build-older "" -DLOCKSTEP_MODE=update -DLOCKSTEP_TEST_COMPILER_VERSION=21.0.0.100)
expect_summary(build-older "0 hits, 1 misses, 0 skipped")
build_and_run(build-older)

run_configure(build-newer "" -DLOCKSTEP_TEST_COMPILER_VERSION=21.0.0.200)
expect_summary(build-newer "1 hits \\(1 built by another toolchain\\), 0 misses, 0 skipped")

run_configure(build-environment-off LOCKSTEP_LIBRARY_CACHE=OFF)
if (step_output MATCHES "lockstep: library cache:")
    message(FATAL_ERROR "LOCKSTEP_LIBRARY_CACHE=OFF in the environment did not beat lockstep.json")
endif ()

run_configure(build-cache-variable-on LOCKSTEP_LIBRARY_CACHE=OFF -DLOCKSTEP_LIBRARY_CACHE=ON
        -DLOCKSTEP_TEST_COMPILER_VERSION=21.0.0.200 -DLOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE=)
expect_summary(build-cache-variable-on "0 hits, 1 misses, 0 skipped")

file(WRITE "${WORK_DIR}/pinned-project/lockstep.lock"
        "{\"bootstrap\": \"\", \"packages\": [], \"root\": \"\", \"version\": 1}\n")
file(WRITE "${WORK_DIR}/pinned-project/lockstep.json"
        [=[{"libraryCache": {"sdkRange": ">=26.5", "locationMaxSize": "1 GiB"}}]=])
run_cache_command(stats)
expect_output("LOCKSTEP_LIBRARY_CACHE_SDK_RANGE +>=26.5 +[^\n]*pinned-project/lockstep.json")
expect_output("LOCKSTEP_LIBRARY_CACHE_MAX_SIZE +5 GiB +the default")
expect_output("limit +5.0 GiB ")
expect_output("evictions +0 ")
run_cache_command_with_environment("LOCKSTEP_LIBRARY_CACHE_LOCATION=${WORK_DIR}/location" stats)
expect_output("location limit +1.0 GiB ")

expect_configure_to_stop(build-bad-size
        "LOCKSTEP_LIBRARY_CACHE_MAX_SIZE from a -D on the build directory: 'lots' is not a size"
        -DLOCKSTEP_LIBRARY_CACHE=ON -DLOCKSTEP_LIBRARY_CACHE_MAX_SIZE=lots)

file(WRITE "${settings_file}" [=[{"libraryCache": {"enabeld": true}}]=])
expect_configure_to_stop(build-unknown-key
        "${settings_file}: lockstep does not know the key \"libraryCache.enabeld\"")
