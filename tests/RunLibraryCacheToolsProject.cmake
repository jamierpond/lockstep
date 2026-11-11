# Builds a project whose package has a static library, a build tool that links
# it and writes a source the project compiles, and an object library the
# project takes through $<TARGET_OBJECTS:...>, with LOCKSTEP_LIBRARY_CACHE on.
# A second build directory must take all three from the store and compile none
# of the package's sources, and the cached tool must write the same source. A
# change to the library the tool links must miss the library and the tool but
# not the object library. A build with OTHER_GENERATOR, when given, must hit
# what the first generator stored. A library cache location then carries all
# three to a machine that has none of them: with SSH_HOST set it is
# <SSH_HOST>:<directory>, and the test prints lockstep-test-skipped when
# `ssh -o BatchMode=yes <SSH_HOST> true` fails; otherwise a directory. Expects
# PROJECT_DIR, WORK_DIR, LOCKSTEP_EXECUTABLE, LOCKSTEP_SHIM and GENERATOR.

include("${CMAKE_CURRENT_LIST_DIR}/LibraryCacheProject.cmake")

set(send_patience_in_tenths_of_a_second 600)
set(ssh_connect_timeout_in_seconds 5)
set(stored_entry_count 3)
set(package_source_count 4)
set(changed_library_source_count 2)
set(package_sources "(number|generator|first_object|second_object)\\.cpp")

set(all_missed "static libraries: 0 hits, 1 misses, 0 skipped; executables: 0 hits, 1 misses, 0 skipped; object libraries: 0 hits, 1 misses, 0 skipped")
set(all_hit "static libraries: 1 hits, 0 misses, 0 skipped; executables: 1 hits, 0 misses, 0 skipped; object libraries: 1 hits, 0 misses, 0 skipped")
set(library_changed "static libraries: 0 hits, 1 misses, 0 skipped; executables: 0 hits, 1 misses, 0 skipped; object libraries: 1 hits, 0 misses, 0 skipped")

function(run_ssh)
    execute_process(COMMAND ssh -n -o BatchMode=yes -o ConnectTimeout=${ssh_connect_timeout_in_seconds}
            "${SSH_HOST}" ${ARGN}
            RESULT_VARIABLE exit_code OUTPUT_VARIABLE output ERROR_VARIABLE output)
    set(ssh_exit_code "${exit_code}" PARENT_SCOPE)
    set(ssh_output "${output}" PARENT_SCOPE)
endfunction()

# Configures with the test's compile recorder, which logs every compile the
# build really runs to <build>-compiles.txt.
function(configure_recorded build expected_summary)
    configure("${build}" "${expected_summary}"
            "-DSAMPLE_COMPILE_RECORD=${WORK_DIR}/${build}-compiles.txt" ${ARGN})
    set(step_output "${step_output}" PARENT_SCOPE)
endfunction()

function(expect_package_compiles build expected_count)
    set(record "${WORK_DIR}/${build}-compiles.txt")
    set(compiles)
    if (EXISTS "${record}")
        file(STRINGS "${record}" compiles REGEX "${package_sources}")
    endif ()
    list(LENGTH compiles count)
    if (NOT count EQUAL expected_count)
        message(FATAL_ERROR "${build} compiled ${count} of the package's sources, not ${expected_count}:\n${compiles}")
    endif ()
endfunction()

function(get_generated_source build out)
    file(READ "${WORK_DIR}/${build}/sample_generated.cpp" text)
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(count_location_entries location_directory out)
    if (SSH_HOST)
        run_ssh("ls ${location_directory}/format-*/*/manifest.json 2>/dev/null | wc -l")
        string(STRIP "${ssh_output}" count)
    else ()
        file(GLOB entries "${location_directory}/format-*/*/manifest.json")
        list(LENGTH entries count)
    endif ()
    set(${out} "${count}" PARENT_SCOPE)
endfunction()

# Sending runs in the background after the configure, so the entries appear later.
function(wait_for_location_entries location_directory expected_count)
    foreach (attempt RANGE ${send_patience_in_tenths_of_a_second})
        count_location_entries("${location_directory}" count)
        if (count EQUAL expected_count)
            return()
        endif ()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.1)
    endforeach ()
    message(FATAL_ERROR "${location_directory} holds ${count} entries, not ${expected_count}")
endfunction()

if (SSH_HOST)
    run_ssh(true)
    if (NOT ssh_exit_code EQUAL 0)
        message("lockstep-test-skipped: ssh -o BatchMode=yes ${SSH_HOST} true failed:\n${ssh_output}")
        return()
    endif ()
endif ()

create_toolmaker_fixture()
file(WRITE "${WORK_DIR}/pinned-project/lockstep.lock"
        "{\"bootstrap\": \"\", \"packages\": [], \"root\": \"\", \"version\": 1}\n")

configure_recorded(build-first "${all_missed}" -DLOCKSTEP_MODE=update)
build_and_run(build-first)
expect_package_compiles(build-first ${package_source_count})
file(GLOB stored "${WORK_DIR}/cache/lib/format-*/*/manifest.json")
list(LENGTH stored stored_count)
if (NOT stored_count EQUAL stored_entry_count)
    message(FATAL_ERROR "the first build stored ${stored_count} entries, not ${stored_entry_count}")
endif ()

configure_recorded(build-second "${all_hit}")
build_and_run(build-second)
expect_package_compiles(build-second 0)
get_generated_source(build-first first_generated)
get_generated_source(build-second second_generated)
if (NOT first_generated STREQUAL second_generated)
    message(FATAL_ERROR "the cached tool wrote:\n${second_generated}\nthe built one wrote:\n${first_generated}")
endif ()

configure_recorded(build-library-changed "${library_changed}" -DSAMPLE_TOOL_OFFSET=1)
build_and_run(build-library-changed)
expect_package_compiles(build-library-changed ${changed_library_source_count})
get_generated_source(build-library-changed changed_generated)
if (NOT changed_generated MATCHES "return 6;")
    message(FATAL_ERROR "the rebuilt tool wrote:\n${changed_generated}")
endif ()

# NMake writes each compile through an inline response file, so its compile
# rule, and with it every key, differs from Ninja's: it stores its own entries
# first, and its second build directory hits them.
if (OTHER_GENERATOR)
    set(CONFIGURE_GENERATOR "${OTHER_GENERATOR}")
    if (OTHER_GENERATOR MATCHES "NMake")
        configure_recorded(build-other-generator-first "${all_missed}")
        build_and_run(build-other-generator-first)
        expect_package_compiles(build-other-generator-first ${package_source_count})
    endif ()
    configure_recorded(build-other-generator "${all_hit}")
    build_and_run(build-other-generator)
    expect_package_compiles(build-other-generator 0)
    unset(CONFIGURE_GENERATOR)
endif ()

set(stored_executable_count 2)
set(stored_object_library_count 1)
if (OTHER_GENERATOR MATCHES "NMake")
    math(EXPR stored_executable_count "${stored_executable_count} + 1")
    math(EXPR stored_object_library_count "${stored_object_library_count} + 1")
endif ()
run_cache_command(stats)
expect_output("executables +${stored_executable_count} +stored now")
expect_output("object libraries +${stored_object_library_count} +stored now")

if (SSH_HOST)
    string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
    set(location_directory ".cache/lockstep-tests/library-cache-tools-${suffix}")
    set(location "${SSH_HOST}:${location_directory}")
else ()
    set(location_directory "${WORK_DIR}/location")
    set(location "${location_directory}")
endif ()

# This machine has all three; the configure sends them to the empty location.
configure(build-send "${all_hit}" "-DLOCKSTEP_LIBRARY_CACHE_LOCATION=${location}")
wait_for_location_entries("${location_directory}" ${stored_entry_count})

run_cache_command(clear)
configure_recorded(build-fetch "${all_hit}" "-DLOCKSTEP_LIBRARY_CACHE_LOCATION=${location}")
expect_output("fetched 3 of the 3 static libraries, executables and object libraries this machine had no copy of")
build_and_run(build-fetch)
expect_package_compiles(build-fetch 0)
get_generated_source(build-fetch fetched_generated)
if (NOT fetched_generated STREQUAL first_generated)
    message(FATAL_ERROR "the fetched tool wrote:\n${fetched_generated}")
endif ()

if (SSH_HOST)
    run_ssh("rm -rf ${location_directory}")
endif ()
