# The steps the library cache tests share: running a configure, a build or a
# cache command and checking its output, and the greeter package fixture.

function(git)
    execute_process(COMMAND git -c user.name=lockstep -c user.email=lockstep@example.com ${ARGN}
            RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if (NOT exit_code EQUAL 0)
        message(FATAL_ERROR "git ${ARGN} failed:\n${output}")
    endif ()
endfunction()

function(run_step)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE exit_code
            OUTPUT_VARIABLE output ERROR_VARIABLE output)
    message("${output}")
    if (NOT exit_code EQUAL 0)
        message(FATAL_ERROR "step failed: ${ARGN}")
    endif ()
    set(step_output "${output}" PARENT_SCOPE)
endfunction()

# Runs cmake with `cmake -E env <environment>` in front, so the library cache
# setting can come from the environment alone.
function(run_configure build environment)
    run_step("${CMAKE_COMMAND}" -E env --unset=LOCKSTEP_LIBRARY_CACHE
            --unset=LOCKSTEP_LIBRARY_CACHE_LOCATION ${environment}
            "${CMAKE_COMMAND}" -S "${PROJECT_DIR}" -B "${WORK_DIR}/${build}" -G "${GENERATOR}"
            "-DLOCKSTEP_SHIM=${LOCKSTEP_SHIM}" "-DLOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
            "-DLOCKSTEP_CACHE=${WORK_DIR}/cache" "-DLOCKSTEP_LOCK_FILE=${WORK_DIR}/lockstep.lock"
            "-DFIXTURE_URL=${fixture_url}" -DCMAKE_BUILD_TYPE=Release ${ARGN})
    set(step_output "${step_output}" PARENT_SCOPE)
endfunction()

function(expect_summary build expected_summary)
    if (NOT step_output MATCHES "lockstep: library cache: ${expected_summary}")
        message(FATAL_ERROR "${build} did not report 'library cache: ${expected_summary}'")
    endif ()
endfunction()

function(configure build expected_summary)
    run_configure("${build}" "" -DLOCKSTEP_LIBRARY_CACHE=ON ${ARGN})
    expect_summary("${build}" "${expected_summary}")
    set(step_output "${step_output}" PARENT_SCOPE)
endfunction()

# Runs `lockstep cache <ARGN>` with `cmake -E env <environment>` in front.
function(run_cache_command_with_environment environment)
    run_step("${CMAKE_COMMAND}" -E chdir "${WORK_DIR}/pinned-project"
            "${CMAKE_COMMAND}" -E env --unset=LOCKSTEP_EXECUTABLE
            --unset=LOCKSTEP_LIBRARY_CACHE_LOCATION ${environment}
            "${LOCKSTEP_EXECUTABLE}" cache ${ARGN} "--cache=${WORK_DIR}/cache")
    set(step_output "${step_output}" PARENT_SCOPE)
endfunction()

function(run_cache_command)
    run_cache_command_with_environment("" ${ARGN})
    set(step_output "${step_output}" PARENT_SCOPE)
endfunction()

function(expect_output pattern)
    if (NOT step_output MATCHES "${pattern}")
        message(FATAL_ERROR "expected output matching '${pattern}'")
    endif ()
endfunction()

function(build_and_run build)
    run_step("${CMAKE_COMMAND}" --build "${WORK_DIR}/${build}")
    run_step("${WORK_DIR}/${build}/sample${CMAKE_EXECUTABLE_SUFFIX}")
endfunction()

# Creates the greeter package as a git repository in WORK_DIR and sets fixture_url.
macro(create_greeter_fixture)
file(REMOVE_RECURSE "${WORK_DIR}")

set(fixture "${WORK_DIR}/fixtures/greeter.git")
file(WRITE "${fixture}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.25)
project(greeter LANGUAGES C CXX)
option(GREETER_LOUD "Configure a louder greeting into greeter_config.h" OFF)
configure_file(greeter_config.h.in "${CMAKE_CURRENT_BINARY_DIR}/config/greeter_config.h")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated/greeter_generated.h")
add_custom_command(OUTPUT "${generated}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/generated.h.in" "${generated}"
        DEPENDS generated.h.in)
add_library(greeter STATIC greeter.cpp greeter_offset.c "${generated}")
target_include_directories(greeter PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/generated"
        PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/config")
]=])
file(WRITE "${fixture}/greeter_config.h.in" "#cmakedefine01 GREETER_LOUD\n")
file(WRITE "${fixture}/generated.h.in" "constexpr auto greeterGenerated = 7;\n")
file(WRITE "${fixture}/include/greeter/greeter.h" "int greet();\n")
file(WRITE "${fixture}/greeter_offset.c" "int greeter_offset(void) { return 0; }\n")
file(WRITE "${fixture}/greeter.cpp" [=[
#include <greeter/greeter.h>
#include "greeter_config.h"

extern "C" int greeter_offset();

int greet()
{
    return (GREETER_LOUD ? 100 : 42) + GREETER_EXTRA + greeter_offset();
}
]=])
git(-C "${fixture}" init --quiet -b main)
git(-C "${fixture}" add -A)
git(-C "${fixture}" commit --quiet -m fixture)

set(fixture_url "file://${fixture}")
if (CMAKE_HOST_WIN32)
    set(fixture_url "file:///${fixture}")
endif ()
endmacro()
