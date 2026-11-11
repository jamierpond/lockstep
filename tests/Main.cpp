#include "FakeArchiver.h"
#include "FakeTools.h"
#include "LibrarySender.h"
#include "StatisticsWriter.h"

#include <NanoTest/NanoTest.h>

#include <optional>
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

bool isFirstArgument(std::span<char*> arguments, std::string_view argument)
{
    return arguments.size() > 1 && arguments[1] == argument;
}

// The test binary started in place of a tool a test makes lockstep run.
std::optional<int> runAsFakeTool(std::span<char*> arguments)
{
    if (isFirstArgument(arguments, fakeArchiverMode))
        return runFakeArchiver();

    if (isFirstArgument(arguments, fakeSshFirstArgument))
        return runFakeSsh(arguments);

    if (isRunAsFakeCompiler(arguments))
        return runFakeCompiler();

    return std::nullopt;
}
} // namespace

int main(int argc, char* argv[])
{
    auto arguments = std::span {argv, static_cast<std::size_t>(argc)};

    if (isRunAs(arguments, statisticsWriterArgument, statisticsWriterArgumentCount))
        return runStatisticsWriter(arguments[2]);

    if (isRunAs(arguments, librarySenderArgument, librarySenderArgumentCount))
        return runLibrarySender({arguments[2], arguments[3], arguments[4]});

    if (auto exitCode = runAsFakeTool(arguments))
        return *exitCode;

    return nano::run(argc, argv);
}
