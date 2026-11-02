# Written by `lockstep cmake-module`, included by the bootstrap in project(). Each
# command forwards to the binary, which writes the CMake to run to _lockstep/add.cmake.
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

macro(_lockstep_execute)
    execute_process(COMMAND ${_LOCKSTEP_COMMAND} ${ARGN}
            RESULT_VARIABLE _lockstep_result ERROR_VARIABLE _lockstep_error)
    string(STRIP "${_lockstep_error}" _lockstep_error)
    if (_lockstep_error)
        message("${_lockstep_error}")
    endif ()
    if (NOT _lockstep_result EQUAL 0)
        message(FATAL_ERROR "lockstep stopped the configure; its message is above.")
    endif ()
endmacro()

_lockstep_execute(begin ${_lockstep_overrides}
        "--top-level-includes=${CMAKE_PROJECT_TOP_LEVEL_INCLUDES}")

macro(_lockstep_run command)
    _lockstep_execute(${command} "--requested-by=${CMAKE_CURRENT_SOURCE_DIR}"
            "--requested-by-file=${CMAKE_CURRENT_LIST_FILE}"
            "--output=${CMAKE_BINARY_DIR}/_lockstep/add.cmake" -- ${ARGN})
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

# include(CPM) finds this stub; a dependency's CPM.cmake returns when CPM_INITIALIZED.
file(WRITE "${CMAKE_BINARY_DIR}/_lockstep/cpm/CPM.cmake" "# lockstep defines CPMAddPackage\n")
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_BINARY_DIR}/_lockstep/cpm")
set(CPM_DIRECTORY "${CMAKE_BINARY_DIR}/_lockstep/cpm")
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
