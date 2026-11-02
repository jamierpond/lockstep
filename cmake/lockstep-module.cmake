# Written by `lockstep cmake-module`, included by the bootstrap in project(). Each
# command forwards to the binary, which writes the CMake to run to _lockstep/add.cmake.
include_guard(GLOBAL)

set(LOCKSTEP_MODE locked CACHE STRING "locked, update or offline")
option(LOCKSTEP_CI "Refuse <Name>_SOURCE overrides in locked mode" OFF)
set(LOCKSTEP_LOCK_FILE "${CMAKE_SOURCE_DIR}/lockstep.lock" CACHE FILEPATH
        "The lock file lockstep reads and, in update mode, writes")

set(_LOCKSTEP_COMMAND "${CMAKE_COMMAND}" -E env "LOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
        "${LOCKSTEP_EXECUTABLE}" "--mode=${LOCKSTEP_MODE}" "--ci=${LOCKSTEP_CI}"
        "--lock=${LOCKSTEP_LOCK_FILE}" "--cache=${LOCKSTEP_CACHE}" "--cmake=${CMAKE_COMMAND}"
        "--source-dir=${CMAKE_SOURCE_DIR}" "--build-dir=${CMAKE_BINARY_DIR}"
        "--bootstrap=${LOCKSTEP_BOOTSTRAP}")

get_cmake_property(_lockstep_variables VARIABLES)
list(FILTER _lockstep_variables INCLUDE REGEX "^(LOCKSTEP|CPM)_.+_SOURCE$")
set(_lockstep_overrides)
foreach (variable IN LISTS _lockstep_variables)
    list(APPEND _lockstep_overrides "--override=${variable}=${${variable}}")
endforeach ()

macro(_lockstep_report)
    string(STRIP "${_lockstep_error}" _lockstep_error)
    if (_lockstep_error)
        message("${_lockstep_error}")
    endif ()
    if (NOT _lockstep_result EQUAL 0)
        message(FATAL_ERROR "lockstep stopped the configure; its message is above.")
    endif ()
endmacro()

execute_process(COMMAND ${_LOCKSTEP_COMMAND} begin ${_lockstep_overrides}
        "--top-level-includes=${CMAKE_PROJECT_TOP_LEVEL_INCLUDES}"
        RESULT_VARIABLE _lockstep_result ERROR_VARIABLE _lockstep_error)
_lockstep_report()
# Pasted into each command, so the arguments expand once and "a\;b" stays whole.
set(_lockstep_run [=[
    execute_process(COMMAND ${_LOCKSTEP_COMMAND} ${_lockstep_subcommand}
            "--requested-by=${CMAKE_CURRENT_SOURCE_DIR}" "--requested-by-file=${CMAKE_CURRENT_LIST_FILE}"
            "--requested-binary-dir=${CMAKE_CURRENT_BINARY_DIR}"
            "--output=${CMAKE_BINARY_DIR}/_lockstep/add.cmake" -- ${_lockstep_arguments}
            RESULT_VARIABLE _lockstep_result ERROR_VARIABLE _lockstep_error)
    _lockstep_report()
    include("${CMAKE_BINARY_DIR}/_lockstep/add.cmake")]=])
# CPMFindPackage always fetches, never find_package(), so the build stays hermetic.
foreach (command lockstep_add_package CPMAddPackage CPMFindPackage)
    cmake_language(EVAL CODE "function(${command})
        set(_lockstep_subcommand add)
        set(_lockstep_arguments \"\${ARGN}\")\n${_lockstep_run}\nendfunction()")
endforeach ()
function(CPMDeclarePackage name)
    set_property(GLOBAL PROPERTY LOCKSTEP_DECLARED_${name} "${ARGN}")
endfunction()
function(CPMGetPackage name)
    get_property(declared GLOBAL PROPERTY LOCKSTEP_DECLARED_${name})
    set(_lockstep_subcommand add)
    set(_lockstep_arguments NAME ${name} ${declared})
    cmake_language(EVAL CODE "${_lockstep_run}")
endfunction()
function(CPMUsePackageLock)
endfunction()
# include(CPM) finds this stub; a dependency's CPM.cmake returns when CPM_INITIALIZED.
file(WRITE "${CMAKE_BINARY_DIR}/_lockstep/cpm/CPM.cmake" "# lockstep defines CPMAddPackage\n")
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_BINARY_DIR}/_lockstep/cpm")
set(CPM_DIRECTORY "${CMAKE_BINARY_DIR}/_lockstep/cpm")
set(CPM_VERSION 999999.0.0)
set(CPM_FILE "${LOCKSTEP_BOOTSTRAP}")
set(CPM_DOWNLOAD_LOCATION "${LOCKSTEP_BOOTSTRAP}")
set_property(GLOBAL PROPERTY CPM_INITIALIZED true)
macro(_lockstep_provide method name)
    set(_lockstep_subcommand provide)
    set(_lockstep_arguments NAME ${name} ${ARGN})
    cmake_language(EVAL CODE "${_lockstep_run}")
endmacro()
cmake_language(SET_DEPENDENCY_PROVIDER _lockstep_provide
        SUPPORTED_METHODS FETCHCONTENT_MAKEAVAILABLE_SERIAL)
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL
        execute_process COMMAND ${_LOCKSTEP_COMMAND} root --announce)
