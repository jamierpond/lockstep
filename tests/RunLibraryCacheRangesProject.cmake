# Builds the library cache project with the greeter package claiming one
# compiler version, then configures fresh build directories claiming another:
# with no range the stored build misses, and LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE
# from a -D or the environment admits it, counted apart and named at VERBOSE.
# An empty -D beats the environment, a range that does not admit the version
# misses and names it, a range that is not one stops the configure, and with
# link-time optimisation a build from a newer compiler is refused whatever the
# range says. Expects PROJECT_DIR, WORK_DIR, LOCKSTEP_EXECUTABLE, LOCKSTEP_SHIM
# and GENERATOR.

include("${CMAKE_CURRENT_LIST_DIR}/LibraryCacheProject.cmake")

set(older_version 21.0.0.100)
set(newer_version 21.0.0.200)

function(expect_text text)
    string(REGEX REPLACE "[ \n]+" " " output "${step_output}")
    string(REGEX REPLACE "[ \n]+" " " text "${text}")
    string(FIND "${output}" "${text}" position)
    if (position EQUAL -1)
        message(FATAL_ERROR "expected output holding '${text}'")
    endif ()
endfunction()

function(expect_no_compile build)
    file(GLOB_RECURSE compiled "${WORK_DIR}/${build}/*greeter.cpp.o*")
    if (compiled)
        message(FATAL_ERROR "the hit still compiled greeter.cpp: ${compiled}")
    endif ()
endfunction()

create_greeter_fixture()
file(WRITE "${WORK_DIR}/pinned-project/lockstep.lock"
        "{\"bootstrap\": \"\", \"packages\": [], \"root\": \"\", \"version\": 1}\n")

configure(build-older "0 hits, 1 misses, 0 skipped" -DLOCKSTEP_MODE=update
        -DLOCKSTEP_TEST_COMPILER_VERSION=${older_version})
build_and_run(build-older)

configure(build-newer "0 hits, 1 misses, 0 skipped" --log-level=VERBOSE
        -DLOCKSTEP_TEST_COMPILER_VERSION=${newer_version})
expect_text("a stored build is not taken: it was built with C compiler ${older_version}, CXX compiler ${older_version}, and neither LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE nor LOCKSTEP_LIBRARY_CACHE_SDK_RANGE is set")

configure(build-newer-range "1 hits \\(1 built by another toolchain\\), 0 misses, 0 skipped"
        --log-level=VERBOSE -DLOCKSTEP_TEST_COMPILER_VERSION=${newer_version}
        "-DLOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE=>=21.0.0")
expect_text("library cache hit for greeter")
expect_text("built with C compiler ${older_version}, CXX compiler ${older_version}")
build_and_run(build-newer-range)
expect_no_compile(build-newer-range)

run_configure(build-environment-range "LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE=<=21.0.0.150"
        -DLOCKSTEP_LIBRARY_CACHE=ON -DLOCKSTEP_TEST_COMPILER_VERSION=${newer_version})
expect_summary(build-environment-range "1 hits \\(1 built by another toolchain\\), 0 misses, 0 skipped")

run_configure(build-empty-beats-environment "LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE=>=21"
        -DLOCKSTEP_LIBRARY_CACHE=ON -DLOCKSTEP_TEST_COMPILER_VERSION=${newer_version}
        -DLOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE=)
expect_summary(build-empty-beats-environment "0 hits, 1 misses, 0 skipped")

configure(build-outside-range "0 hits, 1 misses, 0 skipped" --log-level=VERBOSE
        -DLOCKSTEP_TEST_COMPILER_VERSION=${newer_version}
        "-DLOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE==21.0.1")
expect_text("its C compiler ${older_version} is outside the compiler range =21.0.1")

execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE
        "${CMAKE_COMMAND}" -S "${PROJECT_DIR}" -B "${WORK_DIR}/build-bad-range" -G "${GENERATOR}"
        "-DLOCKSTEP_SHIM=${LOCKSTEP_SHIM}" "-DLOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
        "-DLOCKSTEP_CACHE=${WORK_DIR}/cache" "-DLOCKSTEP_LOCK_FILE=${WORK_DIR}/lockstep.lock"
        "-DFIXTURE_URL=${fixture_url}" -DLOCKSTEP_LIBRARY_CACHE=ON
        "-DLOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE=~21"
        RESULT_VARIABLE exit_code OUTPUT_VARIABLE step_output ERROR_VARIABLE step_output)
if (exit_code EQUAL 0)
    message(FATAL_ERROR "a compiler range of ~21 did not stop the configure")
endif ()
expect_text("LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE from a -D on the build directory: '~21' is not a version range")

run_cache_command(stats)
expect_output("another toolchain +2 ")

# Link-time optimisation: a build from a newer compiler holds intermediate code
# an older linker cannot read, so no range admits it.
run_cache_command(clear)
configure(build-optimised-newer "0 hits, 1 misses, 0 skipped"
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON -DLOCKSTEP_TEST_COMPILER_VERSION=${newer_version})
build_and_run(build-optimised-newer)
configure(build-optimised-older "0 hits, 1 misses, 0 skipped" --log-level=VERBOSE
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON -DLOCKSTEP_TEST_COMPILER_VERSION=${older_version}
        "-DLOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE=>=21")
expect_text("it holds link-time optimisation code from C compiler ${newer_version}")
