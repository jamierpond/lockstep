#include "LibrarySender.h"
#include "StatisticsWriter.h"

#include <NanoTest/NanoTest.h>

#include <span>
#include <string_view>

namespace
{
constexpr auto statisticsWriterArgumentCount = 3;
constexpr auto librarySenderArgumentCount = 5;

bool isRunAs(std::span<char*> arguments, std::string_view mode, int argumentCount)
{
    return std::ssize(arguments) == argumentCount && arguments[1] == mode;
}
} // namespace

int main(int argc, char* argv[])
{
    auto arguments = std::span {argv, static_cast<std::size_t>(argc)};

    if (isRunAs(arguments, statisticsWriterArgument, statisticsWriterArgumentCount))
        return runStatisticsWriter(arguments[2]);

    if (isRunAs(arguments, librarySenderArgument, librarySenderArgumentCount))
        return runLibrarySender({arguments[2], arguments[3], arguments[4]});

    return nano::run(argc, argv);
}
