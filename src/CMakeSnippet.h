#pragma once

#include <string>
#include <string_view>

namespace lockstep
{
// `text` as a quoted CMake argument: backslashes, quotes and `$` escaped so
// CMake reads back exactly `text`.
std::string quoteForCMake(std::string_view text);
} // namespace lockstep
