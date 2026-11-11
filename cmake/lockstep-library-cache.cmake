# Written by `lockstep cmake-module` beside lockstep-module.cmake, which includes it
# when LOCKSTEP_LIBRARY_CACHE is ON. At the end of the top-level configure it writes
# every target's compile settings to _lockstep/library-cache/targets.txt; the binary
# answers with the CMake that links the static libraries it has cached.
include_guard(GLOBAL)

set(_lockstep_target_properties
        SOURCES INTERFACE_SOURCES LINK_LIBRARIES INTERFACE_LINK_LIBRARIES
        COMPILE_OPTIONS COMPILE_DEFINITIONS COMPILE_FLAGS INCLUDE_DIRECTORIES COMPILE_FEATURES
        INTERFACE_COMPILE_OPTIONS INTERFACE_COMPILE_DEFINITIONS INTERFACE_INCLUDE_DIRECTORIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES INTERFACE_COMPILE_FEATURES
        INTERFACE_PRECOMPILE_HEADERS INTERFACE_POSITION_INDEPENDENT_CODE
        PRECOMPILE_HEADERS PRECOMPILE_HEADERS_REUSE_FROM POSITION_INDEPENDENT_CODE SYSTEM
        C_STANDARD C_STANDARD_REQUIRED C_EXTENSIONS C_VISIBILITY_PRESET
        CXX_STANDARD CXX_STANDARD_REQUIRED CXX_EXTENSIONS CXX_VISIBILITY_PRESET
        OBJC_STANDARD OBJC_EXTENSIONS OBJC_VISIBILITY_PRESET
        OBJCXX_STANDARD OBJCXX_EXTENSIONS OBJCXX_VISIBILITY_PRESET
        VISIBILITY_INLINES_HIDDEN INTERPROCEDURAL_OPTIMIZATION
        MSVC_RUNTIME_LIBRARY MSVC_DEBUG_INFORMATION_FORMAT
        UNITY_BUILD UNITY_BUILD_BATCH_SIZE UNITY_BUILD_MODE
        UNITY_BUILD_CODE_BEFORE_INCLUDE UNITY_BUILD_CODE_AFTER_INCLUDE
        AUTOMOC AUTOUIC AUTORCC CXX_MODULE_SETS CXX_SCAN_FOR_MODULES
        STATIC_LIBRARY_OPTIONS LINKER_LANGUAGE)
set(_lockstep_source_properties
        LANGUAGE COMPILE_OPTIONS COMPILE_DEFINITIONS COMPILE_FLAGS INCLUDE_DIRECTORIES
        SKIP_PRECOMPILE_HEADERS SKIP_UNITY_BUILD_INCLUSION UNITY_GROUP)
set(_lockstep_directory_properties COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES)
set(_lockstep_language_settings
        COMPILER COMPILER_ID COMPILER_VERSION COMPILER_TARGET COMPILER_ARCHITECTURE_ID
        COMPILER_FRONTEND_VARIANT COMPILER_EXTERNAL_TOOLCHAIN SIMULATE_ID SIMULATE_VERSION
        STANDARD_DEFAULT EXTENSIONS_DEFAULT BYTE_ORDER COMPILE_OBJECT
        ARCHIVE_CREATE ARCHIVE_APPEND ARCHIVE_FINISH CREATE_STATIC_LIBRARY)
set(_lockstep_directory_settings
        CMAKE_VERSION CMAKE_GENERATOR CMAKE_BUILD_TYPE CMAKE_SYSTEM_NAME CMAKE_SYSTEM_VERSION
        CMAKE_SYSTEM_PROCESSOR CMAKE_LIBRARY_ARCHITECTURE CMAKE_SIZEOF_VOID_P
        CMAKE_CROSSCOMPILING CMAKE_TOOLCHAIN_FILE CMAKE_SYSROOT CMAKE_SYSROOT_COMPILE
        CMAKE_OSX_SYSROOT CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_OSX_ARCHITECTURES
        CMAKE_MSVC_RUNTIME_LIBRARY CMAKE_AR CMAKE_RANLIB APPLE)

# Takes the name of the variable holding the value, so a list in it is never
# split or unescaped on the way in.
function(_lockstep_add_record kind subject field value_variable)
    set(value "${${value_variable}}")
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\n" "\\n" value "${value}")
    string(REPLACE "\r" "\\r" value "${value}")
    string(REPLACE "\t" "\\t" value "${value}")
    set_property(GLOBAL APPEND_STRING PROPERTY _LOCKSTEP_LIBRARY_RECORDS
            "${kind}\t${subject}\t${field}\t${value}\n")
endfunction()

function(_lockstep_describe_linked_targets target property field)
    get_property(items TARGET "${target}" PROPERTY "${property}")
    string(REGEX MATCHALL "[A-Za-z0-9_.+-]+(::[A-Za-z0-9_.+-]+)*" names "${items}")
    set(linked)
    foreach (name IN LISTS names)
        if (NOT TARGET "${name}")
            continue()
        endif ()
        get_property(aliased TARGET "${name}" PROPERTY ALIASED_TARGET)
        if (aliased)
            set(name "${aliased}")
        endif ()
        list(APPEND linked "${name}")
        get_property(is_imported TARGET "${name}" PROPERTY IMPORTED)
        if (is_imported)
            _lockstep_describe_target("${name}")
        endif ()
    endforeach ()
    list(REMOVE_DUPLICATES linked)
    _lockstep_add_record(target "${target}" "${field}" linked)
endfunction()

function(_lockstep_find_source_language target source out)
    set(${out} "" PARENT_SCOPE)
    get_source_file_property(header_only "${source}" TARGET_DIRECTORY "${target}" HEADER_FILE_ONLY)
    if (header_only)
        return()
    endif ()
    get_source_file_property(language "${source}" TARGET_DIRECTORY "${target}" LANGUAGE)
    if (language)
        set(${out} "${language}" PARENT_SCOPE)
        return()
    endif ()
    get_source_file_property(external_object "${source}" TARGET_DIRECTORY "${target}" EXTERNAL_OBJECT)
    cmake_path(GET source EXTENSION LAST_ONLY extension)
    string(REGEX REPLACE "^\\." "" extension "${extension}")
    if (external_object OR extension MATCHES "^(o|obj)$")
        set(${out} OBJECT PARENT_SCOPE)
        return()
    endif ()
    get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
    get_property(directory TARGET "${target}" PROPERTY SOURCE_DIR)
    foreach (candidate IN LISTS languages)
        get_directory_property(extensions DIRECTORY "${directory}"
                DEFINITION "CMAKE_${candidate}_SOURCE_FILE_EXTENSIONS")
        if (extension IN_LIST extensions)
            set(${out} "${candidate}" PARENT_SCOPE)
            return()
        endif ()
    endforeach ()
endfunction()

# Which sources a cached build drops (the compiled ones), which it keeps, and the
# language of the empty stub that stands in for the dropped ones.
function(_lockstep_describe_sources target)
    get_property(sources TARGET "${target}" PROPERTY SOURCES)
    set(kept)
    set(settings)
    set(compiled_languages)
    foreach (source IN LISTS sources)
        if (source MATCHES "\\$<")
            _lockstep_add_record(target "${target}" GENERATOR_EXPRESSION_SOURCE source)
            return()
        endif ()
        _lockstep_find_source_language("${target}" "${source}" language)
        if (NOT language)
            list(APPEND kept "${source}")
            continue()
        endif ()
        list(APPEND compiled_languages "${language}")
        foreach (property IN LISTS _lockstep_source_properties)
            get_source_file_property(value "${source}" TARGET_DIRECTORY "${target}" "${property}")
            if (NOT value STREQUAL "NOTFOUND")
                string(APPEND settings "${source} ${property}=${value}\n")
            endif ()
        endforeach ()
    endforeach ()
    get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
    set(stub_language)
    if (compiled_languages MATCHES "CXX" AND CXX IN_LIST languages)
        set(stub_language CXX)
    elseif (compiled_languages MATCHES "(^|;)(C|OBJC)(;|$)" AND C IN_LIST languages)
        set(stub_language C)
    endif ()
    list(REMOVE_DUPLICATES compiled_languages)
    _lockstep_add_record(target "${target}" COMPILED_LANGUAGES compiled_languages)
    _lockstep_add_record(target "${target}" KEPT_SOURCES kept)
    _lockstep_add_record(target "${target}" COMPILED_SOURCE_SETTINGS settings)
    _lockstep_add_record(target "${target}" STUB_LANGUAGE stub_language)
endfunction()

function(_lockstep_describe_link_launcher target)
    get_property(launcher TARGET "${target}" PROPERTY RULE_LAUNCH_LINK)
    if (NOT launcher)
        get_property(directory TARGET "${target}" PROPERTY SOURCE_DIR)
        get_property(launcher DIRECTORY "${directory}" PROPERTY RULE_LAUNCH_LINK)
    endif ()
    if (NOT launcher)
        get_property(launcher GLOBAL PROPERTY RULE_LAUNCH_LINK)
    endif ()
    _lockstep_add_record(target "${target}" EXISTING_LINK_LAUNCHER launcher)
endfunction()

function(_lockstep_describe_target target)
    get_property(is_described GLOBAL PROPERTY "_LOCKSTEP_DESCRIBED_${target}")
    if (is_described)
        return()
    endif ()
    set_property(GLOBAL PROPERTY "_LOCKSTEP_DESCRIBED_${target}" TRUE)
    foreach (property IN ITEMS TYPE IMPORTED SOURCE_DIR BINARY_DIR ${_lockstep_target_properties})
        get_property(value TARGET "${target}" PROPERTY "${property}")
        if (NOT value STREQUAL "")
            _lockstep_add_record(target "${target}" "${property}" value)
        endif ()
    endforeach ()
    _lockstep_describe_linked_targets("${target}" LINK_LIBRARIES LINKED_TARGETS)
    _lockstep_describe_linked_targets("${target}" INTERFACE_LINK_LIBRARIES INTERFACE_LINKED_TARGETS)
    get_property(type TARGET "${target}" PROPERTY TYPE)
    get_property(is_imported TARGET "${target}" PROPERTY IMPORTED)
    if (is_imported)
        return()
    endif ()
    if (type STREQUAL "STATIC_LIBRARY")
        _lockstep_describe_sources("${target}")
    elseif (type MATCHES "^(EXECUTABLE|SHARED_LIBRARY|MODULE_LIBRARY)$")
        _lockstep_describe_link_launcher("${target}")
    endif ()
endfunction()

# The variables a package's CMake could have read: the binary keeps those whose
# names appear in the package's CMake files.
function(_lockstep_describe_variables directory)
    get_property(names DIRECTORY "${directory}" PROPERTY VARIABLES)
    get_property(cached GLOBAL PROPERTY CACHE_VARIABLES)
    list(APPEND names ${cached})
    list(FILTER names INCLUDE REGEX "^[A-Z][A-Z0-9_]*$")
    list(FILTER names EXCLUDE REGEX "^(CMAKE|LOCKSTEP|CPM)_")
    list(REMOVE_DUPLICATES names)
    foreach (name IN LISTS names)
        get_directory_property(value DIRECTORY "${directory}" DEFINITION "${name}")
        _lockstep_add_record(variable "${directory}" "${name}" value)
    endforeach ()
endfunction()

# The variables a compiler step reads in the directory that declares the target:
# a subproject's project() can enable a language the top level never enabled.
function(_lockstep_list_directory_setting_names out)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" configuration)
    get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
    list(REMOVE_ITEM languages NONE)
    set(names ${_lockstep_directory_settings})
    foreach (language IN LISTS languages)
        foreach (setting IN LISTS _lockstep_language_settings)
            list(APPEND names "CMAKE_${language}_${setting}")
        endforeach ()
        list(APPEND names "CMAKE_${language}_FLAGS")
        if (configuration)
            list(APPEND names "CMAKE_${language}_FLAGS_${configuration}")
        endif ()
    endforeach ()
    set(${out} "${names}" PARENT_SCOPE)
endfunction()

# The SDK the compiler really uses on Apple systems, by version and build: with
# CMake 4, CMAKE_OSX_SYSROOT is empty and the compiler picks its default SDK,
# which CMake resolves into _CMAKE_OSX_SYSROOT_PATH the way /usr/bin/cc does.
function(_lockstep_describe_apple_sdk directory)
    get_directory_property(sysroot DIRECTORY "${directory}" DEFINITION _CMAKE_OSX_SYSROOT_PATH)
    if (NOT sysroot)
        get_directory_property(sysroot DIRECTORY "${directory}" DEFINITION CMAKE_OSX_SYSROOT)
    endif ()
    string(MD5 sysroot_id "${sysroot}")
    get_property(is_known GLOBAL PROPERTY "_LOCKSTEP_APPLE_SDK_KNOWN_${sysroot_id}")
    if (NOT is_known)
        set(sdk_arguments)
        if (sysroot)
            set(sdk_arguments --sdk "${sysroot}")
        endif ()
        execute_process(COMMAND xcrun ${sdk_arguments} --show-sdk-version
                OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND xcrun ${sdk_arguments} --show-sdk-build-version
                OUTPUT_VARIABLE build_version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        set_property(GLOBAL PROPERTY "_LOCKSTEP_APPLE_SDK_KNOWN_${sysroot_id}" TRUE)
        set_property(GLOBAL PROPERTY "_LOCKSTEP_APPLE_SDK_VERSION_${sysroot_id}" "${version}")
        set_property(GLOBAL PROPERTY "_LOCKSTEP_APPLE_SDK_BUILD_VERSION_${sysroot_id}" "${build_version}")
    endif ()
    get_property(version GLOBAL PROPERTY "_LOCKSTEP_APPLE_SDK_VERSION_${sysroot_id}")
    get_property(build_version GLOBAL PROPERTY "_LOCKSTEP_APPLE_SDK_BUILD_VERSION_${sysroot_id}")
    _lockstep_add_record(directory "${directory}" APPLE_SDK_VERSION version)
    _lockstep_add_record(directory "${directory}" APPLE_SDK_BUILD_VERSION build_version)
endfunction()

function(_lockstep_describe_directory directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach (target IN LISTS targets)
        _lockstep_describe_target("${target}")
    endforeach ()
    if (targets)
        foreach (property IN LISTS _lockstep_directory_properties)
            get_property(value DIRECTORY "${directory}" PROPERTY "${property}")
            _lockstep_add_record(directory "${directory}" "${property}" value)
        endforeach ()
        _lockstep_list_directory_setting_names(setting_names)
        foreach (name IN LISTS setting_names)
            get_directory_property(value DIRECTORY "${directory}" DEFINITION "${name}")
            _lockstep_add_record(directory "${directory}" "${name}" value)
        endforeach ()
        get_directory_property(is_apple DIRECTORY "${directory}" DEFINITION APPLE)
        if (is_apple)
            _lockstep_describe_apple_sdk("${directory}")
        endif ()
        _lockstep_describe_variables("${directory}")
    endif ()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach (subdirectory IN LISTS subdirectories)
        _lockstep_describe_directory("${subdirectory}")
    endforeach ()
endfunction()

function(_lockstep_describe_globals)
    get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
    list(REMOVE_ITEM languages NONE)
    _lockstep_add_record(global - ENABLED_LANGUAGES languages)
endfunction()

function(_lockstep_apply_library_cache)
    get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if (is_multi_config)
        message(STATUS "lockstep: library cache off: ${CMAKE_GENERATOR} builds several "
                "configurations from one build directory, which the cache does not handle yet")
        return()
    endif ()
    set_property(GLOBAL PROPERTY _LOCKSTEP_LIBRARY_RECORDS "")
    _lockstep_describe_globals()
    _lockstep_describe_directory("${CMAKE_SOURCE_DIR}")
    get_property(records GLOBAL PROPERTY _LOCKSTEP_LIBRARY_RECORDS)
    set(input "${CMAKE_BINARY_DIR}/_lockstep/library-cache/targets.txt")
    file(WRITE "${input}" "${records}")
    set(output "${CMAKE_BINARY_DIR}/_lockstep/library-cache/apply.cmake")
    # A -DLOCKSTEP_LIBRARY_CACHE_LOCATION on the build directory, even an empty
    # one, wins over the environment variable, as LOCKSTEP_LIBRARY_CACHE does.
    set(location "$ENV{LOCKSTEP_LIBRARY_CACHE_LOCATION}")
    if (DEFINED CACHE{LOCKSTEP_LIBRARY_CACHE_LOCATION})
        set(location "${LOCKSTEP_LIBRARY_CACHE_LOCATION}")
    endif ()
    execute_process(COMMAND ${_LOCKSTEP_COMMAND} library-cache "--input=${input}" "--output=${output}"
            "--library-cache-location=${location}"
            RESULT_VARIABLE _lockstep_result ERROR_VARIABLE _lockstep_error)
    _lockstep_report()
    include("${output}")
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _lockstep_apply_library_cache)
