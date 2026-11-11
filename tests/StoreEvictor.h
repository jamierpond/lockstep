#pragma once

#include <string_view>

// The test binary run with this argument, a cache directory and a size limit in
// bytes evicts that cache's library store down to the limit instead of running
// tests, so a test can start two of it at once.
constexpr auto storeEvictorArgument = std::string_view {"--evict-library-store"};

int runStoreEvictor(std::string_view cacheDirectory, std::string_view limitBytes);
