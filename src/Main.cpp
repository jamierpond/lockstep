#include "Commands.h"
#include "PackageArguments.h"
#include "LibraryCacheSettings.h"
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
#include <array>
#include <filesystem>
#include <map>
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
  cache stats    print the library cache's stored libraries, size, hits and
                 misses
  cache clear    delete the library cache's stored libraries and counts
  begin, add, provide, library-cache
                 run by the CMake module during configure; add and provide take
                 the package's CMake arguments after --
  use-cached-library, use-cached-objects, store-libraries
                 run by the build in place of, or around, the link or compile
                 command after --, when LOCKSTEP_LIBRARY_CACHE is ON
  send-libraries started by store-libraries and library-cache to copy
                 libraries to LOCKSTEP_LIBRARY_CACHE_LOCATION)";

constexpr auto argumentSeparator = std::string_view {"--"};
constexpr auto machineWideCommand = std::string_view {"cache"};

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

// The cache commands act on the machine's store, not a project, so this binary
// runs them whichever release the project pins.
bool isMachineWideCommand(const eacp::Vector<std::string>& arguments)
{
    auto isOption = [](const std::string& argument)
    { return argument.starts_with('-'); };
    auto command = std::ranges::find_if_not(arguments, isOption);
    return command != arguments.end() && *command == machineWideCommand;
}

std::optional<int> runProjectPinnedVersionInstead()
{
    if (eacp::getEnv("LOCKSTEP_EXECUTABLE")
        || isMachineWideCommand(getArgumentsAfterProgram()))
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

using CommandFunction = int (*)(const lockstep::CommandOptions&);

constexpr auto commandFunctions =
    std::array<std::pair<std::string_view, CommandFunction>, 13> {
        {{"begin", lockstep::runBeginCommand},
         {"add", lockstep::runAddCommand},
         {"provide", lockstep::runProvideCommand},
         {"update", lockstep::runUpdateCommand},
         {"verify", lockstep::runVerifyCommand},
         {"root", lockstep::runRootCommand},
         {"cmake-module", lockstep::runCMakeModuleCommand},
         {"library-cache", lockstep::runLibraryCacheCommand},
         {"use-cached-library", lockstep::runUseCachedLibraryCommand},
         {"use-cached-objects", lockstep::runUseCachedObjectsCommand},
         {"store-libraries", lockstep::runStoreLibrariesCommand},
         {"send-libraries", lockstep::runSendLibrariesCommand},
         {machineWideCommand, lockstep::runCacheCommand}}};

// The build passes the location its configure read; the cache commands, which
// run outside any build, read the environment variable.
std::string getLibraryCacheLocation(const lockstep::CommandOptions& options)
{
    if (options.command != machineWideCommand)
        return options.libraryCacheLocation;

    return eacp::getEnvValue("LOCKSTEP_LIBRARY_CACHE_LOCATION");
}

std::map<std::string, std::string> readSettingEnvironment()
{
    auto environment = std::map<std::string, std::string> {};

    for (const auto& name: lockstep::getSettingVariableNames())
        environment[name] = eacp::getEnvValue(name);

    return environment;
}

int runCommand(lockstep::CommandOptions options)
{
    options.cache = getCacheDirectory(options.cache);
    options.settingEnvironment = readSettingEnvironment();
    options.libraryCacheLocation = getLibraryCacheLocation(options);
    options.lock = getLockFile(options);

    for (const auto& [name, function]: commandFunctions)
    {
        if (options.command == name)
            return function(options);
    }

    throw std::runtime_error("unknown command '" + options.command
                             + "'; run lockstep --help for the list");
}

void addPathOptions(eacp::Args::Parser& parser, lockstep::CommandOptions& options)
{
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
    parser.option("--output",
                  options.output,
                  "the file add, provide, cmake-module and library-cache write; the "
                  "library use-cached-library writes");
    parser.option(
        "--input", options.input, "the target descriptions library-cache reads");
}

void addBehaviourOptions(eacp::Args::Parser& parser,
                         lockstep::CommandOptions& options,
                         std::string& ciValue)
{
    parser.option(
        "--mode", options.mode, "locked, update or offline [default: locked]");
    parser.option(
        "--ci", ciValue, "ON refuses <Name>_SOURCE overrides in locked mode");
    parser.option("--library",
                  options.libraries,
                  "a cached library for use-cached-library; <key>=<library> for "
                  "store-libraries");
    parser.option("--executable",
                  options.executables,
                  "<key>=<executable> the link store-libraries runs writes");
    parser.option("--object-library",
                  options.objectLibraries,
                  "<key>=<object files> of an object library, for store-libraries");
    parser.option("--stored-object",
                  options.storedObjects,
                  "a cached object file for use-cached-objects, in --objects order");
    parser.option("--objects",
                  options.objects,
                  "the object library's object files, a ;-list, for "
                  "use-cached-objects");
    parser.option("--object",
                  options.object,
                  "the object file the compile writes, for use-cached-objects");
    parser.option("--library-cache-location",
                  options.libraryCacheLocation,
                  "a directory or [user@]host:directory holding the library cache, "
                  "which the build passes on from LOCKSTEP_LIBRARY_CACHE_LOCATION");
    parser.option("--cache-variable",
                  options.cacheVariables,
                  "<name>=<value> of a library cache setting the build directory "
                  "defines, for begin and library-cache");
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
        options.argumentsAfterSeparator.assign(separator + 1, arguments.end());
        arguments.getVector().erase(separator, arguments.end());
    }

    auto ciValue = std::string {};
    auto parser = eacp::Args::Parser {"lockstep", toolDescription};

    parser.positional(options.command, "command", "see the list below");
    parser.positional(options.names,
                      "name",
                      "the packages update re-resolves; stats or clear after cache");
    addPathOptions(parser, options);
    addBehaviourOptions(parser, options, ciValue);
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
