#pragma once

#include <string_view>

// The test binary run with this argument and a cache directory adds to that
// cache's library statistics instead of running tests, so a test can start two
// of it at once.
constexpr auto statisticsWriterArgument = std::string_view {"--add-statistics"};
constexpr auto statisticsWritesPerProcess = 200;

int runStatisticsWriter(std::string_view cacheDirectory);
