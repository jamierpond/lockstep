# lockstep: hermetic CMake dependencies with a lock file and content hashes.
# Load it during the top-level project() call, before project() runs:
#
#   set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)
#   project(MyApp)
#   lockstep_add_package(NAME Miro GITHUB eyalamirmusic/Miro GIT_TAG main)

cmake_minimum_required(VERSION 3.24)

get_property(_lockstep_loaded GLOBAL PROPERTY LOCKSTEP_SHIM_LOADED)
if (_lockstep_loaded)
    return()
endif ()
set_property(GLOBAL PROPERTY LOCKSTEP_SHIM_LOADED TRUE)

set(LOCKSTEP_VERSION 0.1.0)
set(LOCKSTEP_RELEASE_URL https://github.com/jamierpond/lockstep/releases/download)
set(LOCKSTEP_SHA256_macos-arm64 "")
set(LOCKSTEP_SHA256_macos-x64 "")
set(LOCKSTEP_SHA256_windows-x64 "")
set(LOCKSTEP_SHA256_windows-arm64 "")
set(LOCKSTEP_SHA256_linux-x64 "")
set(LOCKSTEP_SHA256_linux-arm64 "")

set(LOCKSTEP_SHIM_FILE "${CMAKE_CURRENT_LIST_FILE}")

set(LOCKSTEP_MODE locked CACHE STRING
        "locked: fetch exactly what lockstep.lock records; update: re-resolve refs and rewrite the lock; offline: use only the cache")
set_property(CACHE LOCKSTEP_MODE PROPERTY STRINGS locked update offline)
option(LOCKSTEP_CI "Refuse LOCKSTEP_<Name>_SOURCE overrides in locked mode" OFF)
set(LOCKSTEP_LOCK_FILE "${CMAKE_SOURCE_DIR}/lockstep.lock" CACHE FILEPATH
        "The lock file lockstep reads and, in update mode, writes")
set(LOCKSTEP_EXECUTABLE "" CACHE FILEPATH
        "A lockstep binary to run instead of downloading the pinned release")

if (NOT LOCKSTEP_MODE MATCHES "^(locked|update|offline)$")
    message(FATAL_ERROR "lockstep: LOCKSTEP_MODE is '${LOCKSTEP_MODE}'; use locked, update or offline")
endif ()

function(_lockstep_find_project_top_level_includes result)
    set(${result} FALSE PARENT_SCOPE)
    file(REAL_PATH "${LOCKSTEP_SHIM_FILE}" shim)

    foreach (entry IN LISTS CMAKE_PROJECT_TOP_LEVEL_INCLUDES)
        if (EXISTS "${entry}")
            file(REAL_PATH "${entry}" entry)

            if (entry STREQUAL shim)
                set(${result} TRUE PARENT_SCOPE)
            endif ()
        endif ()
    endforeach ()
endfunction()

_lockstep_find_project_top_level_includes(_lockstep_loaded_by_project)

if (NOT _lockstep_loaded_by_project)
    message(FATAL_ERROR
            "lockstep: ${LOCKSTEP_SHIM_FILE} was include()d after project(), too late to route FetchContent_MakeAvailable through lockstep. "
            "Load it during the top-level project() instead, by putting\n"
            "  set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES \${CMAKE_CURRENT_LIST_DIR}/<path>/lockstep.cmake)\n"
            "before project() in ${CMAKE_SOURCE_DIR}/CMakeLists.txt, and remove the include().")
endif ()

function(_lockstep_get_default_cache_directory result)
    if (CMAKE_HOST_WIN32)
        file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/lockstep" directory)
    elseif (NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
        set(directory "$ENV{XDG_CACHE_HOME}/lockstep")
    else ()
        set(directory "$ENV{HOME}/.cache/lockstep")
    endif ()

    set(${result} "${directory}" PARENT_SCOPE)
endfunction()

if (NOT DEFINED LOCKSTEP_CACHE AND NOT "$ENV{LOCKSTEP_CACHE}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{LOCKSTEP_CACHE}" _lockstep_cache_from_environment)
    set(LOCKSTEP_CACHE "${_lockstep_cache_from_environment}" CACHE PATH
            "The shared lockstep cache")
endif ()
_lockstep_get_default_cache_directory(_lockstep_default_cache)
set(LOCKSTEP_CACHE "${_lockstep_default_cache}" CACHE PATH "The shared lockstep cache")

function(_lockstep_get_host_platform result)
    if (CMAKE_HOST_APPLE)
        set(system macos)
    elseif (CMAKE_HOST_WIN32)
        set(system windows)
    elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(system linux)
    else ()
        message(FATAL_ERROR "lockstep: no release binary for ${CMAKE_HOST_SYSTEM_NAME}; set LOCKSTEP_EXECUTABLE to a local build")
    endif ()

    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" processor)

    if (processor MATCHES "^(arm64|aarch64)$")
        set(architecture arm64)
    elseif (processor MATCHES "^(x86_64|amd64)$")
        set(architecture x64)
    else ()
        message(FATAL_ERROR "lockstep: no release binary for the ${CMAKE_HOST_SYSTEM_PROCESSOR} processor; set LOCKSTEP_EXECUTABLE to a local build")
    endif ()

    set(${result} "${system}-${architecture}" PARENT_SCOPE)
endfunction()

function(_lockstep_download_pinned_executable result)
    _lockstep_get_host_platform(platform)
    set(expected_hash "${LOCKSTEP_SHA256_${platform}}")

    if (expected_hash STREQUAL "")
        message(FATAL_ERROR "lockstep: this shim pins no ${platform} binary for ${LOCKSTEP_VERSION}; set LOCKSTEP_EXECUTABLE to a local build")
    endif ()

    set(asset "lockstep-${LOCKSTEP_VERSION}-${platform}")
    if (CMAKE_HOST_WIN32)
        string(APPEND asset ".exe")
    endif ()

    set(executable "${LOCKSTEP_CACHE}/bin/${LOCKSTEP_VERSION}/${asset}")

    if (NOT EXISTS "${executable}")
        message(STATUS "lockstep: downloading ${asset}")
        file(DOWNLOAD "${LOCKSTEP_RELEASE_URL}/v${LOCKSTEP_VERSION}/${asset}" "${executable}.part"
                EXPECTED_HASH SHA256=${expected_hash}
                STATUS status)
        list(GET status 0 code)

        if (NOT code EQUAL 0)
            file(REMOVE "${executable}.part")
            message(FATAL_ERROR "lockstep: downloading ${asset} failed: ${status}")
        endif ()

        file(CHMOD "${executable}.part" PERMISSIONS
                OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
        file(RENAME "${executable}.part" "${executable}")
    endif ()

    file(SHA256 "${executable}" actual_hash)
    if (NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR "lockstep: ${executable} hashes to ${actual_hash}, not the pinned ${expected_hash}; delete it to download it again")
    endif ()

    set(${result} "${executable}" PARENT_SCOPE)
endfunction()

if (LOCKSTEP_EXECUTABLE STREQUAL "" AND NOT "$ENV{LOCKSTEP_EXECUTABLE}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{LOCKSTEP_EXECUTABLE}" _lockstep_executable_from_environment)
    set_property(CACHE LOCKSTEP_EXECUTABLE PROPERTY VALUE "${_lockstep_executable_from_environment}")
endif ()

if (LOCKSTEP_EXECUTABLE STREQUAL "")
    _lockstep_download_pinned_executable(_lockstep_executable)
else ()
    set(_lockstep_executable "${LOCKSTEP_EXECUTABLE}")
endif ()
set_property(GLOBAL PROPERTY LOCKSTEP_RUNNING_EXECUTABLE "${_lockstep_executable}")

function(_lockstep_run_tool)
    get_property(executable GLOBAL PROPERTY LOCKSTEP_RUNNING_EXECUTABLE)
    execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "LOCKSTEP_EXECUTABLE=${executable}"
            "${executable}" ${ARGN}
            --lock "${LOCKSTEP_LOCK_FILE}" --cache "${LOCKSTEP_CACHE}"
            RESULT_VARIABLE exit_code
            OUTPUT_VARIABLE output
            ERROR_VARIABLE errors
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE)

    if (NOT exit_code EQUAL 0)
        message(FATAL_ERROR "${errors}")
    endif ()

    if (NOT errors STREQUAL "")
        message(STATUS "${errors}")
    endif ()

    set(_lockstep_tool_output "${output}" PARENT_SCOPE)
endfunction()

function(_lockstep_get_package_name_from_url url result)
    string(REGEX REPLACE "[?#].*$" "" path "${url}")
    string(REGEX REPLACE "/+$" "" path "${path}")
    get_filename_component(name "${path}" NAME)
    string(REGEX REPLACE "(\\.git|\\.zip|\\.tar\\.gz|\\.tgz|\\.tar\\.xz|\\.tar\\.bz2|\\.tar)$" "" name "${name}")
    set(${result} "${name}" PARENT_SCOPE)
endfunction()

function(_lockstep_apply_options)
    foreach (option IN LISTS ARGN)
        string(REGEX MATCH "^([^ ]+) +(.*)$" matched "${option}")

        if (matched STREQUAL "")
            message(FATAL_ERROR "lockstep: OPTIONS entry '${option}' is not 'NAME VALUE'")
        endif ()

        set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}" PARENT_SCOPE)
    endforeach ()
endfunction()

macro(_lockstep_return_package_variables name)
    set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
    set(${name}_BINARY_DIR "${${name}_BINARY_DIR}" PARENT_SCOPE)
    set(${name}_ADDED "${${name}_ADDED}" PARENT_SCOPE)
    set(CPM_LAST_PACKAGE_NAME "${name}" PARENT_SCOPE)
    set(CPM_PACKAGE_${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
    set(CPM_PACKAGE_${name}_BINARY_DIR "${${name}_BINARY_DIR}" PARENT_SCOPE)
endmacro()

function(_lockstep_resolve_package_source name kind url ref result)
    set(override "${LOCKSTEP_${name}_SOURCE}")
    if (override STREQUAL "")
        set(override "$ENV{LOCKSTEP_${name}_SOURCE}")
    endif ()

    if (NOT override STREQUAL "")
        if (LOCKSTEP_CI AND LOCKSTEP_MODE STREQUAL "locked")
            message(FATAL_ERROR "lockstep: LOCKSTEP_${name}_SOURCE overrides ${name} with ${override}, which LOCKSTEP_CI forbids in locked mode")
        endif ()

        get_filename_component(override "${override}" ABSOLUTE)
        message(WARNING "lockstep: ${name} comes from the local override LOCKSTEP_${name}_SOURCE=${override}, not from the lock")
        set(${result} "${override}" PARENT_SCOPE)
        return()
    endif ()

    set(patch_arguments)
    foreach (patch IN LISTS ARGN)
        get_filename_component(patch "${patch}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND patch_arguments --patch "${patch}")
    endforeach ()

    _lockstep_run_tool(resolve
            --name "${name}" --kind "${kind}" --url "${url}" "--ref=${ref}"
            --mode "${LOCKSTEP_MODE}" --cmake "${CMAKE_COMMAND}" --shim "${LOCKSTEP_SHIM_FILE}"
            --requester "${PROJECT_NAME} (${CMAKE_CURRENT_SOURCE_DIR})"
            ${patch_arguments})
    set(${result} "${_lockstep_tool_output}" PARENT_SCOPE)
endfunction()

function(_lockstep_add_package)
    cmake_parse_arguments(PARSE_ARGV 0 ARG
            "DOWNLOAD_ONLY;EXCLUDE_FROM_ALL;SYSTEM"
            "NAME;GITHUB;GITHUB_REPOSITORY;GITLAB_REPOSITORY;GIT_REPOSITORY;GIT_TAG;VERSION;URL;SOURCE_SUBDIR"
            "OPTIONS;PATCHES")

    if (DEFINED ARG_GITHUB_REPOSITORY)
        set(ARG_GITHUB "${ARG_GITHUB_REPOSITORY}")
    endif ()

    if (DEFINED ARG_GITHUB)
        set(kind git)
        set(url "https://github.com/${ARG_GITHUB}.git")
    elseif (DEFINED ARG_GITLAB_REPOSITORY)
        set(kind git)
        set(url "https://gitlab.com/${ARG_GITLAB_REPOSITORY}.git")
    elseif (DEFINED ARG_GIT_REPOSITORY)
        set(kind git)
        set(url "${ARG_GIT_REPOSITORY}")
    elseif (DEFINED ARG_URL)
        set(kind archive)
        set(url "${ARG_URL}")
    else ()
        message(FATAL_ERROR "lockstep: a package needs GITHUB, GITHUB_REPOSITORY, GITLAB_REPOSITORY, GIT_REPOSITORY or URL")
    endif ()

    set(name "${ARG_NAME}")
    if (name STREQUAL "")
        _lockstep_get_package_name_from_url("${url}" name)
    endif ()

    set(ref "${ARG_GIT_TAG}")
    if (kind STREQUAL "git" AND ref STREQUAL "")
        if (DEFINED ARG_VERSION)
            set(ref "v${ARG_VERSION}")
        else ()
            message(FATAL_ERROR "lockstep: ${name} needs a GIT_TAG or a VERSION")
        endif ()
    endif ()

    get_property(added_ref GLOBAL PROPERTY LOCKSTEP_PACKAGE_${name}_REF SET)
    if (added_ref)
        get_property(added_ref GLOBAL PROPERTY LOCKSTEP_PACKAGE_${name}_REF)
        if (NOT added_ref STREQUAL ref)
            message(WARNING "lockstep: ${PROJECT_NAME} asks for ${name} at '${ref}', but it was already added at '${added_ref}', which is kept")
        endif ()

        get_property(${name}_SOURCE_DIR GLOBAL PROPERTY LOCKSTEP_PACKAGE_${name}_SOURCE_DIR)
        get_property(${name}_BINARY_DIR GLOBAL PROPERTY LOCKSTEP_PACKAGE_${name}_BINARY_DIR)
        set(${name}_ADDED NO)
        _lockstep_return_package_variables(${name})
        set(_lockstep_last_name "${name}" PARENT_SCOPE)
        return()
    endif ()

    _lockstep_resolve_package_source("${name}" "${kind}" "${url}" "${ref}" source ${ARG_PATCHES})

    string(TOLOWER "${name}" lower_name)
    set(${name}_SOURCE_DIR "${source}")
    set(${name}_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/${lower_name}-build")
    set(${name}_ADDED YES)

    set_property(GLOBAL PROPERTY LOCKSTEP_PACKAGE_${name}_REF "${ref}")
    set_property(GLOBAL PROPERTY LOCKSTEP_PACKAGE_${name}_SOURCE_DIR "${${name}_SOURCE_DIR}")
    set_property(GLOBAL PROPERTY LOCKSTEP_PACKAGE_${name}_BINARY_DIR "${${name}_BINARY_DIR}")

    set(subdirectory "${source}")
    if (DEFINED ARG_SOURCE_SUBDIR)
        set(subdirectory "${source}/${ARG_SOURCE_SUBDIR}")
    endif ()

    if (NOT ARG_DOWNLOAD_ONLY AND EXISTS "${subdirectory}/CMakeLists.txt")
        _lockstep_apply_options(${ARG_OPTIONS})
        set(add_arguments)
        if (ARG_EXCLUDE_FROM_ALL)
            list(APPEND add_arguments EXCLUDE_FROM_ALL)
        endif ()
        if (ARG_SYSTEM)
            list(APPEND add_arguments SYSTEM)
        endif ()

        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        add_subdirectory("${subdirectory}" "${${name}_BINARY_DIR}" ${add_arguments})
    endif ()

    _lockstep_return_package_variables(${name})
    set(_lockstep_last_name "${name}" PARENT_SCOPE)
endfunction()

function(lockstep_add_package)
    _lockstep_add_package(${ARGV})
    _lockstep_return_package_variables(${_lockstep_last_name})
endfunction()

# CPM's single-string form: "gh:owner/repo@1.2.3", "gl:owner/repo#tag",
# "https://host/repo.git@1.2.3", or "https://host/archive.zip".
function(_lockstep_expand_cpm_shorthand text result)
    set(tag "")
    set(version "")

    if (text MATCHES "^(.*)#([^#]+)$")
        set(text "${CMAKE_MATCH_1}")
        set(tag "${CMAKE_MATCH_2}")
    endif ()

    if (text MATCHES "^(.*[^/:])@([^@/]+)$")
        set(text "${CMAKE_MATCH_1}")
        set(version "${CMAKE_MATCH_2}")
    endif ()

    if (text MATCHES "^gh:(.+)$")
        set(arguments GITHUB_REPOSITORY "${CMAKE_MATCH_1}")
    elseif (text MATCHES "^gl:(.+)$")
        set(arguments GITLAB_REPOSITORY "${CMAKE_MATCH_1}")
    elseif (text MATCHES "\\.git$" OR NOT text MATCHES "\\.(zip|tar|tgz|gz|xz|bz2)$")
        set(arguments GIT_REPOSITORY "${text}")
    else ()
        set(arguments URL "${text}")
    endif ()

    if (NOT tag STREQUAL "")
        list(APPEND arguments GIT_TAG "${tag}")
    elseif (NOT version STREQUAL "")
        list(APPEND arguments VERSION "${version}")
    endif ()

    set(${result} "${arguments}" PARENT_SCOPE)
endfunction()

function(CPMAddPackage)
    set(arguments ${ARGV})
    if (ARGC EQUAL 1)
        _lockstep_expand_cpm_shorthand("${ARGV0}" arguments)
    endif ()

    _lockstep_add_package(${arguments})
    _lockstep_return_package_variables(${_lockstep_last_name})
endfunction()

# Always fetches, never find_package(), so the build stays hermetic.
function(CPMFindPackage)
    CPMAddPackage(${ARGV})
    _lockstep_return_package_variables(${CPM_LAST_PACKAGE_NAME})
endfunction()

function(CPMDeclarePackage name)
    set_property(GLOBAL PROPERTY LOCKSTEP_DECLARED_${name} "${ARGN}")
endfunction()

function(CPMGetPackage name)
    get_property(declared GLOBAL PROPERTY LOCKSTEP_DECLARED_${name})
    _lockstep_add_package(NAME ${name} ${declared})
    _lockstep_return_package_variables(${name})
endfunction()

function(CPMUsePackageLock)
endfunction()

# A dependency's own include(CPM.cmake) returns before defining anything when
# CPM_DIRECTORY names another directory, or CPM_INITIALIZED is set.
set(CPM_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
set(CPM_VERSION 999999.0.0)
set(CPM_FILE "${LOCKSTEP_SHIM_FILE}")
set(CPM_DOWNLOAD_LOCATION "${LOCKSTEP_SHIM_FILE}")
set_property(GLOBAL PROPERTY CPM_INITIALIZED true)

function(lockstep_provide_dependency method name)
    cmake_parse_arguments(PARSE_ARGV 2 ARG
            "EXCLUDE_FROM_ALL;SYSTEM"
            "GIT_REPOSITORY;GIT_TAG;URL;SOURCE_SUBDIR;SOURCE_DIR"
            "PATCH_COMMAND")

    if (DEFINED ARG_SOURCE_DIR AND NOT DEFINED ARG_GIT_REPOSITORY AND NOT DEFINED ARG_URL)
        return()
    endif ()

    if (DEFINED ARG_PATCH_COMMAND)
        message(FATAL_ERROR "lockstep: ${PROJECT_NAME} declares ${name} with PATCH_COMMAND, which lockstep cannot hash; use lockstep_add_package(... PATCHES)")
    endif ()

    set(arguments NAME "${name}")
    foreach (key GIT_REPOSITORY GIT_TAG URL SOURCE_SUBDIR)
        if (DEFINED ARG_${key})
            list(APPEND arguments ${key} "${ARG_${key}}")
        endif ()
    endforeach ()
    foreach (flag EXCLUDE_FROM_ALL SYSTEM)
        if (ARG_${flag})
            list(APPEND arguments ${flag})
        endif ()
    endforeach ()

    if (NOT DEFINED ARG_GIT_REPOSITORY AND NOT DEFINED ARG_URL)
        message(FATAL_ERROR "lockstep: ${PROJECT_NAME} asks FetchContent for ${name} with neither GIT_REPOSITORY nor URL, so lockstep cannot lock it")
    endif ()

    _lockstep_add_package(${arguments})
    FetchContent_SetPopulated(${name}
            SOURCE_DIR "${${name}_SOURCE_DIR}"
            BINARY_DIR "${${name}_BINARY_DIR}")
endfunction()

cmake_language(SET_DEPENDENCY_PROVIDER lockstep_provide_dependency
        SUPPORTED_METHODS FETCHCONTENT_MAKEAVAILABLE_SERIAL)

function(_lockstep_print_root_hash)
    if (EXISTS "${LOCKSTEP_LOCK_FILE}")
        _lockstep_run_tool(root)
        message(STATUS "lockstep: root ${_lockstep_tool_output}")
    endif ()
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _lockstep_print_root_hash)
