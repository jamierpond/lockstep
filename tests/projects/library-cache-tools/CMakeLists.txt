cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${LOCKSTEP_SHIM})
project(LibraryCacheToolsSample LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# The test's compile recorder, so it sees which sources each build compiled.
if (SAMPLE_COMPILE_RECORD)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND}" "-DRECORD_FILE=${SAMPLE_COMPILE_RECORD}"
            -P "${CMAKE_CURRENT_LIST_DIR}/../../RecordCompile.cmake" --)
endif ()

set(SAMPLE_TOOL_OFFSET 0 CACHE STRING "Added to the number the tool writes, through a compile definition on the library it links")

lockstep_add_package(NAME toolmaker GIT_REPOSITORY "${FIXTURE_URL}" GIT_TAG main)
target_compile_definitions(toolmaker_number PRIVATE TOOLMAKER_OFFSET=${SAMPLE_TOOL_OFFSET})

add_executable(sample main.cpp $<TARGET_OBJECTS:toolmaker_objects>)
target_compile_definitions(sample PRIVATE SAMPLE_TOOL_OFFSET=${SAMPLE_TOOL_OFFSET})
toolmaker_generate_source(sample)
