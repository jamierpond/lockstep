# Checks that the bootstrap and lockstep agree on where a pinned binary lives:
# configuring a project downloads it from a file:// release, then lockstep,
# with the release gone, must find it there instead of downloading. Runs once with XDG_CACHE_HOME set and once with
# it empty, which both must treat as unset. Expects WORK_DIR,
# LOCKSTEP_EXECUTABLE and LOCKSTEP_SHIM.

set(pinned_version 9.9.9)
set(platforms macos-arm64 macos-x64 linux-x64 linux-arm64)

function(run_step)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE exit_code
            OUTPUT_VARIABLE output ERROR_VARIABLE output)
    message("${output}")
    if (NOT exit_code EQUAL 0)
        message(FATAL_ERROR "step failed: ${ARGN}")
    endif ()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
set(releases "${WORK_DIR}/releases")
set(project "${WORK_DIR}/project")
file(SHA256 "${LOCKSTEP_EXECUTABLE}" binary_hash)
file(MAKE_DIRECTORY "${releases}/v${pinned_version}")

foreach (platform IN LISTS platforms)
    file(COPY_FILE "${LOCKSTEP_EXECUTABLE}"
            "${releases}/v${pinned_version}/lockstep-${pinned_version}-${platform}")
endforeach ()

file(READ "${LOCKSTEP_SHIM}" shim)
string(REGEX REPLACE "set\\(LOCKSTEP_VERSION [^)]*\\)"
        "set(LOCKSTEP_VERSION ${pinned_version})" shim "${shim}")
string(REGEX REPLACE "set\\(LOCKSTEP_RELEASE_URL [^)]*\\)"
        "set(LOCKSTEP_RELEASE_URL file://${releases})" shim "${shim}")
string(REGEX REPLACE "(set\\(LOCKSTEP_SHA256_[a-z0-9-]+) \"[0-9a-f]*\"\\)"
        "\\1 \"${binary_hash}\")" shim "${shim}")
file(WRITE "${project}/lockstep.cmake" "${shim}")
file(WRITE "${project}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES \${CMAKE_CURRENT_LIST_DIR}/lockstep.cmake)
project(bootstrap-cache LANGUAGES NONE)
")
file(WRITE "${project}/lockstep.lock"
        "{\"bootstrap\": \"lockstep.cmake\", \"packages\": [], \"root\": \"\", \"version\": 1}\n")

foreach (xdg_cache IN ITEMS "${WORK_DIR}/xdg" "")
    file(REMOVE_RECURSE "${WORK_DIR}/xdg" "${WORK_DIR}/home" "${WORK_DIR}/configure")
    file(MAKE_DIRECTORY "${WORK_DIR}/home" "${WORK_DIR}/configure")
    set(environment ${CMAKE_COMMAND} -E env --unset=LOCKSTEP_CACHE
            --unset=LOCKSTEP_EXECUTABLE "HOME=${WORK_DIR}/home"
            "XDG_CACHE_HOME=${xdg_cache}")

    run_step(${environment} ${CMAKE_COMMAND} -S "${project}"
            -B "${WORK_DIR}/configure")

    file(RENAME "${releases}" "${releases}-hidden")
    execute_process(COMMAND ${environment} "${LOCKSTEP_EXECUTABLE}" root
            WORKING_DIRECTORY "${project}" RESULT_VARIABLE exit_code
            OUTPUT_VARIABLE output ERROR_VARIABLE output)
    file(RENAME "${releases}-hidden" "${releases}")
    message("${output}")
    if (NOT exit_code EQUAL 0 OR NOT output MATCHES "runs that instead")
        message(FATAL_ERROR "lockstep did not find the binary the bootstrap fetched")
    endif ()
endforeach ()
