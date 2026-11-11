#pragma once

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Files.h>

#include <cstdint>
#include <limits>
#include <string>

// A size limit no test store reaches, for tests about something else.
constexpr auto unlimitedBytes = std::numeric_limits<std::uint64_t>::max();

inline eacp::FilePath createEmptyTestDirectory(const std::string& name)
{
    auto directory = eacp::FilePath::tempDirectory() / ("lockstep-test-" + name);
    eacp::Files::removeAll(directory);
    eacp::Files::createDirectories(directory);
    return directory;
}

inline void writeTestFile(const eacp::FilePath& path, std::string_view text)
{
    eacp::Files::createDirectories(path.parentDirectory());
    eacp::Files::writeFile(path, text);
}
