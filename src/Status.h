#pragma once

#include <string>

namespace lockstep
{
// Writes "lockstep: <line>" to stderr, where CMake shows it during configure.
void printStatus(const std::string& line);
} // namespace lockstep
