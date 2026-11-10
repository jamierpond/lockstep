cmake_minimum_required(VERSION 3.25)
project(included LANGUAGES CXX)

add_executable(included-tool tool.cpp)
install(TARGETS included-tool DESTINATION included)
