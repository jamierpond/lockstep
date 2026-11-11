# Builds the library cache project with LOCKSTEP_LIBRARY_CACHE_LOCATION set: the
# first build sends greeter's library to the location, and after `lockstep cache
# clear` deletes this machine's copies a fresh build directory fetches it from
# there and links it without compiling. With SSH_HOST set the location is
# <SSH_HOST>:<directory>, and the test prints lockstep-test-skipped when
# `ssh -o BatchMode=yes <SSH_HOST> true` fails. Without it the location is a
# directory, and the test also checks that a configure sends what the location
# lacks, that a changed library at the location is discarded, that an
# unreachable host gives one warning, that a failed send is reported by the next
# configure, and that an empty -DLOCKSTEP_LIBRARY_CACHE_LOCATION beats the
# environment variable. Expects PROJECT_DIR, WORK_DIR, LOCKSTEP_EXECUTABLE,
# LOCKSTEP_SHIM and GENERATOR.

include("${CMAKE_CURRENT_LIST_DIR}/LibraryCacheProject.cmake")

set(send_patience_in_tenths_of_a_second 600)
set(ssh_connect_timeout_in_seconds 5)

# CMake wraps a warning's text, so runs of spaces and line ends compare as one space.
function(expect_text text)
    string(REGEX REPLACE "[ \n]+" " " output "${step_output}")
    string(REGEX REPLACE "[ \n]+" " " text "${text}")
    string(FIND "${output}" "${text}" position)
    if (position EQUAL -1)
        message(FATAL_ERROR "expected output holding '${text}'")
    endif ()
endfunction()

function(configure_at_location build location expected_summary)
    configure("${build}" "${expected_summary}"
            "-DLOCKSTEP_LIBRARY_CACHE_LOCATION=${location}" ${ARGN})
    set(step_output "${step_output}" PARENT_SCOPE)
endfunction()

function(run_ssh)
    execute_process(COMMAND ssh -n -o BatchMode=yes -o ConnectTimeout=${ssh_connect_timeout_in_seconds}
            "${SSH_HOST}" ${ARGN}
            RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output)
    set(ssh_exit_code "${exit_code}" PARENT_SCOPE)
    set(ssh_output "${output}" PARENT_SCOPE)
endfunction()

# Sending runs in the background after the link, so the entry appears later.
function(wait_for_entry location_directory)
    foreach (attempt RANGE ${send_patience_in_tenths_of_a_second})
        if (SSH_HOST)
            run_ssh("ls ${location_directory}/format-*/*/manifest.json")
            set(found "")
            if (ssh_exit_code EQUAL 0)
                set(found "${ssh_output}")
            endif ()
        else ()
            file(GLOB found "${location_directory}/format-*/*/manifest.json")
        endif ()
        if (found)
            return()
        endif ()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.1)
    endforeach ()
    file(GLOB logs "${WORK_DIR}/*/_lockstep/library-cache/send.log")
    foreach (log IN LISTS logs)
        file(READ "${log}" text)
        message("${log}:\n${text}")
    endforeach ()
    message(FATAL_ERROR "no library reached ${location_directory}")
endfunction()

function(wait_for_file path)
    foreach (attempt RANGE ${send_patience_in_tenths_of_a_second})
        if (EXISTS "${path}")
            return()
        endif ()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.1)
    endforeach ()
    message(FATAL_ERROR "${path} did not appear")
endfunction()

function(expect_no_compile build)
    file(GLOB_RECURSE compiled "${WORK_DIR}/${build}/*greeter.cpp.o*")
    if (compiled)
        message(FATAL_ERROR "the fetched library still compiled greeter.cpp: ${compiled}")
    endif ()
endfunction()

if (SSH_HOST)
    run_ssh(true)
    if (NOT ssh_exit_code EQUAL 0)
        message("lockstep-test-skipped: ssh -o BatchMode=yes ${SSH_HOST} true failed:\n${ssh_output}")
        return()
    endif ()
    string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
    set(location_directory ".cache/lockstep-tests/library-cache-location-${suffix}")
    set(location "${SSH_HOST}:${location_directory}")
else ()
    set(location_directory "${WORK_DIR}/location")
    set(location "${location_directory}")
endif ()

create_greeter_fixture()
file(WRITE "${WORK_DIR}/pinned-project/lockstep.lock"
        "{\"bootstrap\": \"\", \"packages\": [], \"root\": \"\", \"version\": 1}\n")

configure_at_location(build-send "${location}" "0 hits, 1 misses, 0 skipped" -DLOCKSTEP_MODE=update)
expect_text("library cache at ${location}: fetched 0 of the 1 libraries this machine had no copy of")
build_and_run(build-send)
wait_for_entry("${location_directory}")

run_cache_command_with_environment("LOCKSTEP_LIBRARY_CACHE_LOCATION=${location}" clear)
expect_text("deleted this machine's copies in")
expect_text("the library cache at ${location} is left as it is")

configure_at_location(build-fetch "${location}" "1 hits, 0 misses, 0 skipped")
expect_text("fetched 1 of the 1 libraries this machine had no copy of")
build_and_run(build-fetch)
expect_no_compile(build-fetch)

run_cache_command_with_environment("LOCKSTEP_LIBRARY_CACHE_LOCATION=${location}" stats)
expect_text("library cache at ${location}; this machine's copies in")
expect_output("fetched +1 ")

if (SSH_HOST)
    run_ssh("rm -rf ${location_directory}")
    return()
endif ()

# This machine has a copy the new location lacks, so the configure sends it.
configure_at_location(build-backfill "${WORK_DIR}/second-location" "1 hits, 0 misses, 0 skipped")
expect_text("sending 1 it lacks")
wait_for_entry("${WORK_DIR}/second-location")

file(GLOB location_libraries "${location_directory}/format-*/*/*greeter*")
file(WRITE "${location_libraries}" "changed after it was sent")
run_cache_command(clear)
configure_at_location(build-changed "${location}" "0 hits, 1 misses, 0 skipped")
expect_text("discarded 1 libraries fetched from ${location}")

configure_at_location(build-unreachable "lockstep-no-such-host.invalid:libraries"
        "0 hits, 1 misses, 0 skipped")
string(REGEX MATCHALL "cannot use the library cache at" warnings "${step_output}")
list(LENGTH warnings warning_count)
if (NOT warning_count EQUAL 1)
    message(FATAL_ERROR "an unreachable host gave ${warning_count} warnings, not 1")
endif ()
build_and_run(build-unreachable)

# A location that is a file cannot hold libraries, so the send after the link fails.
file(WRITE "${WORK_DIR}/location-file" "not a directory")
run_cache_command(clear)
configure_at_location(build-send-fails "${WORK_DIR}/location-file" "0 hits, 1 misses, 0 skipped")
build_and_run(build-send-fails)
wait_for_file("${WORK_DIR}/build-send-fails/_lockstep/library-cache/send-failure.txt")
configure_at_location(build-send-fails "${WORK_DIR}/location-file" "1 hits, 0 misses, 0 skipped")
expect_text("sending libraries to ${WORK_DIR}/location-file after an earlier build failed")

run_configure(build-location-off "LOCKSTEP_LIBRARY_CACHE_LOCATION=${location}"
        -DLOCKSTEP_LIBRARY_CACHE=ON -DLOCKSTEP_LIBRARY_CACHE_LOCATION=)
expect_summary(build-location-off "1 hits, 0 misses, 0 skipped")
string(FIND "${step_output}" "library cache at" position)
if (NOT position EQUAL -1)
    message(FATAL_ERROR "an empty -DLOCKSTEP_LIBRARY_CACHE_LOCATION did not beat the environment variable")
endif ()
