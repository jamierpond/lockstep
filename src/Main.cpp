#include "Commands.h"
#include "PackageArguments.h"
#include "LockFile.h"
#include "PackageCache.h"
#include "PinnedRelease.h"
#include "Status.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/App/AppEnvironment.h>
#include <eacp/Core/App/Args.h>
#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace
{
constexpr auto toolDescription =
    "Fetches CMake dependencies at the exact commits and content hashes recorded in "
    "lockstep.lock.";

constexpr auto commandsHelp = R"(commands:
  update         re-resolve the refs of the named packages (all when none are
                 named) and rewrite the lock
  verify         check each locked package's cache entry still hashes to its
                 lock hash
  root           print the root hash over every package in the lock
  cmake-module   write the CMake module the bootstrap includes to --output
  begin, add, provide
                 run by the CMake module during configure; add and provide take
                 the package's CMake arguments after --)";

constexpr auto argumentSeparator = std::string_view {"--"};

eacp::FilePath getCacheDirectory(const eacp::FilePath& requested)
{
    if (!requested.empty())
        return requested;

    if (auto fromEnvironment = eacp::getEnvValue("LOCKSTEP_CACHE");
        !fromEnvironment.empty())
        return eacp::FilePath {fromEnvironment};

    return lockstep::PackageCache::getDefaultDirectory();
}

eacp::FilePath getLockFile(const lockstep::CommandOptions& options)
{
    if (!options.lock.empty())
        return options.lock;

    if (!options.sourceDirectory.empty())
        return options.sourceDirectory / lockstep::lockFileName;

    return eacp::FilePath {std::filesystem::current_path()} / lockstep::lockFileName;
}

eacp::Vector<std::string> getArgumentsAfterProgram()
{
    auto arguments = eacp::Apps::commandLineArgs();
    arguments.removeAt(0);
    return arguments;
}

std::optional<lockstep::PinnedRelease> findProjectPinnedRelease()
{
    auto lockPath = lockstep::findEnclosingLockFile(std::filesystem::current_path());

    if (!lockPath)
        return std::nullopt;

    auto lock = lockstep::readLockFile(*lockPath);

    if (lock.bootstrap.empty())
        return std::nullopt;

    return lockstep::readPinnedRelease(lockPath->parentDirectory() / lock.bootstrap);
}

std::optional<int> runProjectPinnedVersionInstead()
{
    if (eacp::getEnv("LOCKSTEP_EXECUTABLE"))
        return std::nullopt;

    auto release = findProjectPinnedRelease();

    if (!release || release->version == LOCKSTEP_VERSION)
        return std::nullopt;

    lockstep::printStatus("this project pins lockstep " + release->version
                          + ", so this " + LOCKSTEP_VERSION + " runs that instead");

    auto executable =
        lockstep::fetchPinnedExecutable(*release, getCacheDirectory({}));
    auto options = eacp::Processes::ProcessOptions {};
    options.executable = executable.str();
    options.arguments = getArgumentsAfterProgram();
    options.environment.add({"LOCKSTEP_EXECUTABLE", executable.str()});
    options.captureOutput = false;
    return eacp::Processes::run(options).exitCode;
}

int runCommand(lockstep::CommandOptions options)
{
    options.cache = getCacheDirectory(options.cache);
    options.lock = getLockFile(options);

    if (options.command == "begin")
        return lockstep::runBeginCommand(options);

    if (options.command == "add")
        return lockstep::runAddCommand(options);

    if (options.command == "provide")
        return lockstep::runProvideCommand(options);

    if (options.command == "update")
        return lockstep::runUpdateCommand(options);

    if (options.command == "verify")
        return lockstep::runVerifyCommand(options);

    if (options.command == "root")
        return lockstep::runRootCommand(options);

    if (options.command == "cmake-module")
        return lockstep::runCMakeModuleCommand(options);

    throw std::runtime_error("unknown command '" + options.command
                             + "'; run lockstep --help for the list");
}

int parseAndRunCommand()
{
    if (auto exitCode = runProjectPinnedVersionInstead())
        return *exitCode;

    auto arguments = getArgumentsAfterProgram();

    auto options = lockstep::CommandOptions {};
    auto separator =
        std::find(arguments.begin(), arguments.end(), argumentSeparator);

    if (separator != arguments.end())
    {
        options.packageArguments.assign(separator + 1, arguments.end());
        arguments.getVector().erase(separator, arguments.end());
    }

    auto ciValue = std::string {};
    auto parser = eacp::Args::Parser {"lockstep", toolDescription};

    parser.positional(options.command, "command", "see the list below");
    parser.positional(options.names, "name", "the packages update re-resolves");
    parser.option(
        "--mode", options.mode, "locked, update or offline [default: locked]");
    parser.option(
        "--ci", ciValue, "ON refuses <Name>_SOURCE overrides in locked mode");
    parser.option("--lock",
                  options.lock,
                  "the lock file [default: <source-dir>/lockstep.lock]");
    parser.option(
        "--cache",
        options.cache,
        "the shared cache [default: $LOCKSTEP_CACHE, else the user cache folder]");
    parser.option("--cmake", options.cmake, "the cmake that extracts tar archives");
    parser.option(
        "--source-dir", options.sourceDirectory, "the top-level source directory");
    parser.option(
        "--build-dir", options.buildDirectory, "the top-level build directory");
    parser.option("--bootstrap", options.bootstrap, "the project's lockstep.cmake");
    parser.option(
        "--requested-by", options.requestedBy, "the directory asking for a package");
    parser.option("--requested-by-file",
                  options.requestedByFile,
                  "the CMake file asking for a package");
    parser.option("--requested-binary-dir",
                  options.requestedBinaryDirectory,
                  "its binary directory, which a relative SOURCE_DIR is under");
    parser.option(
        "--output", options.output, "the file add, provide and cmake-module write");
    parser.option(
        "--top-level-includes",
        options.topLevelIncludes,
        "CMAKE_PROJECT_TOP_LEVEL_INCLUDES, which begin checks holds the bootstrap");
    parser.option("--override",
                  options.overrides,
                  "LOCKSTEP_<Name>_SOURCE or CPM_<Name>_SOURCE=<dir>, for begin");
    parser.flag("--full",
                options.isFullVerify,
                "verify rehashes every tree instead of trusting stamps");
    parser.flag("--announce",
                options.shouldAnnounce,
                "root prints '-- lockstep: root <hash>'");
    parser.epilogue(commandsHelp);

    if (auto code = parser.exitCodeFor(parser.parse(arguments)))
        return *code;

    options.isCI = lockstep::isCMakeTrue(ciValue);
    return runCommand(options);
}
} // namespace

int main(int argc, char* argv[])
{
    return eacp::Apps::runConsole(argc, argv, parseAndRunCommand);
}
