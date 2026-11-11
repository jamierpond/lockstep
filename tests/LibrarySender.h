#pragma once

#include <string_view>

// The test binary run with this argument, a cache directory, a library cache
// location (a directory, or <host>:<directory> reached through the test binary
// as ssh), a key and a size limit in bytes sends that key's library to the
// location instead of running tests, so a test can start two of it at once.
constexpr auto librarySenderArgument = std::string_view {"--send-library"};

struct LibrarySenderArguments
{
    std::string_view cacheDirectory;
    std::string_view locationDirectory;
    std::string_view key;
    std::string_view limitBytes;
};

int runLibrarySender(const LibrarySenderArguments& arguments);
