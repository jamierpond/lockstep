cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${LOCKSTEP_SHIM})
project(ExcludeFromAllSample LANGUAGES CXX)

lockstep_add_package(NAME excluded SOURCE_DIR excluded)
lockstep_add_package(NAME included SOURCE_DIR included EXCLUDE_FROM_ALL NO)

add_executable(sample ../github/main.cpp)
target_link_libraries(sample PRIVATE adder)
