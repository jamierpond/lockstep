cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${LOCKSTEP_SHIM})
project(LibraryCacheSample LANGUAGES CXX)

set(SAMPLE_GREETER_EXTRA 0 CACHE STRING "Added to the greeting through a compile definition")

lockstep_add_package(NAME greeter GIT_REPOSITORY "${FIXTURE_URL}" GIT_TAG main)
target_compile_definitions(greeter PRIVATE GREETER_EXTRA=${SAMPLE_GREETER_EXTRA})

add_executable(sample main.cpp)
target_link_libraries(sample PRIVATE greeter)
