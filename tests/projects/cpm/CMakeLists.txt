cmake_minimum_required(VERSION 3.24)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${LOCKSTEP_SHIM})
project(CpmSample LANGUAGES CXX)

# A project that used CPM still says include(CPM); lockstep answers it.
include(CPM)

CPMAddPackage("gh:cpm-cmake/testpack-fibadder@1.1.0")

# CPM writes a find module per package, so find_package finds the added one.
find_package(testpack-fibadder REQUIRED)

add_executable(sample main.cpp)
target_link_libraries(sample PRIVATE fibadder)
