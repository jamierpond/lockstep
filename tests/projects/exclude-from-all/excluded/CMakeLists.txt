cmake_minimum_required(VERSION 3.25)
project(excluded LANGUAGES CXX)

add_library(adder STATIC adder.cpp)
target_include_directories(adder PUBLIC include)

# Nothing links it, so a bare build must never compile it.
add_library(never-built STATIC broken.cpp)
install(FILES adder.cpp DESTINATION excluded)
