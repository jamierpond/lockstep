# lockstep's CMake commands, written by `lockstep cmake-module` and included by
# the bootstrap during the first project(). Each one forwards to the binary,
# which writes the CMake to run into _lockstep/add.cmake.
include_guard(GLOBAL)

set(LOCKSTEP_MODE locked CACHE STRING "locked, update or offline")
option(LOCKSTEP_CI "Refuse LOCKSTEP_<Name>_SOURCE overrides in locked mode" OFF)
set(LOCKSTEP_LOCK_FILE "${CMAKE_SOURCE_DIR}/lockstep.lock" CACHE FILEPATH
        "The lock file lockstep reads and, in update mode, writes")

set(_LOCKSTEP_COMMAND "${CMAKE_COMMAND}" -E env "LOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
        "${LOCKSTEP_EXECUTABLE}" "--mode=${LOCKSTEP_MODE}" "--ci=${LOCKSTEP_CI}"
        "--lock=${LOCKSTEP_LOCK_FILE}" "--cache=${LOCKSTEP_CACHE}" "--cmake=${CMAKE_COMMAND}"
        "--source-dir=${CMAKE_SOURCE_DIR}" "--build-dir=${CMAKE_BINARY_DIR}"
        "--bootstrap=${LOCKSTEP_BOOTSTRAP}")

get_cmake_property(_lockstep_variables VARIABLES)
list(FILTER _lockstep_variables INCLUDE REGEX "^LOCKSTEP_.+_SOURCE$")
set(_lockstep_overrides)
foreach (variable IN LISTS _lockstep_variables)
    list(APPEND _lockstep_overrides "--override=${variable}=${${variable}}")
endforeach ()

execute_process(COMMAND ${_LOCKSTEP_COMMAND} begin ${_lockstep_overrides}
        "--top-level-includes=${CMAKE_PROJECT_TOP_LEVEL_INCLUDES}"
        COMMAND_ERROR_IS_FATAL ANY)

macro(_lockstep_run command)
    execute_process(COMMAND ${_LOCKSTEP_COMMAND} ${command}
            "--requested-by=${CMAKE_CURRENT_SOURCE_DIR}"
            "--output=${CMAKE_BINARY_DIR}/_lockstep/add.cmake" -- ${ARGN}
            COMMAND_ERROR_IS_FATAL ANY)
    include("${CMAKE_BINARY_DIR}/_lockstep/add.cmake")
endmacro()

macro(lockstep_add_package)
    _lockstep_run(add ${ARGV})
endmacro()

macro(CPMAddPackage)
    _lockstep_run(add ${ARGV})
endmacro()

# Always fetches, never find_package(), so the build stays hermetic.
macro(CPMFindPackage)
    _lockstep_run(add ${ARGV})
endmacro()

function(CPMDeclarePackage name)
    set_property(GLOBAL PROPERTY LOCKSTEP_DECLARED_${name} "${ARGN}")
endfunction()

macro(CPMGetPackage name)
    get_property(_lockstep_declared GLOBAL PROPERTY LOCKSTEP_DECLARED_${name})
    _lockstep_run(add NAME ${name} ${_lockstep_declared})
endmacro()

function(CPMUsePackageLock)
endfunction()

# A dependency's own include(CPM.cmake) returns before defining anything when
# CPM_DIRECTORY names another directory, or CPM_INITIALIZED is set.
set(CPM_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
set(CPM_VERSION 999999.0.0)
set(CPM_FILE "${LOCKSTEP_BOOTSTRAP}")
set(CPM_DOWNLOAD_LOCATION "${LOCKSTEP_BOOTSTRAP}")
set_property(GLOBAL PROPERTY CPM_INITIALIZED true)

macro(_lockstep_provide method name)
    _lockstep_run(provide NAME ${name} ${ARGN})
endmacro()

cmake_language(SET_DEPENDENCY_PROVIDER _lockstep_provide
        SUPPORTED_METHODS FETCHCONTENT_MAKEAVAILABLE_SERIAL)

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL
        execute_process COMMAND ${_LOCKSTEP_COMMAND} root --announce)
