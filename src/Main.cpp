#include "Commands.h"
#include "LockFile.h"
#include "PackageCache.h"
#include "PinnedRelease.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/App/Args.h>
#include <eacp/Core/App/AppEnvironment.h>
#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Environment.h>

#include <filesystem>

#include <stdexcept>

namespace
{
constexpr auto toolDescription =
    "Fetches CMake dependencies at the exact commits and content hashes recorded in "
    "lockstep.lock.";

constexpr auto commandsHelp = R"(commands:
  resolve   print the cached source directory of one package (--name, --url, --ref)
  update    re-resolve the refs of the named packages (all when none are named)
            and rewrite the lock
  verify    check each locked package's cache entry still hashes to its lock hash
  root      print the root hash over every package in the lock)";

eacp::FilePath getCacheDirectory(const eacp::FilePath& requested)
{
    if (!requested.empty())
        return requested;

    if (auto fromEnvironment = eacp::getEnv("LOCKSTEP_CACHE");
        fromEnvironment && !fromEnvironment->empty())
        return eacp::FilePath {*fromEnvironment};

    return lockstep::PackageCache::getDefaultDirectory();
}

std::optional<lockstep::PinnedRelease> findProjectPinnedRelease()
{
    auto lockPath = lockstep::findEnclosingLockFile(std::filesystem::current_path());

    if (!lockPath)
        return std::nullopt;

    auto lock = lockstep::readLockFile(*lockPath);

    if (lock.shim.empty())
        return std::nullopt;

    return lockstep::readPinnedRelease(lockPath->parentDirectory() / lock.shim);
}

std::optional<int> runProjectPinnedVersionInstead()
{
    if (eacp::getEnv("LOCKSTEP_EXECUTABLE"))
        return std::nullopt;

    auto release = findProjectPinnedRelease();

    if (!release || release->version == LOCKSTEP_VERSION)
        return std::nullopt;

    eacp::Console::printError("lockstep: this project pins lockstep "
                              + release->version + ", so this " + LOCKSTEP_VERSION
                              + " runs that instead");

    auto executable =
        lockstep::fetchPinnedExecutable(*release, getCacheDirectory({}));
    auto options = eacp::Processes::ProcessOptions {};
    options.executable = executable.str();
    options.arguments = eacp::Apps::commandLineArgs();
    options.arguments.removeAt(0);
    options.environment.add({"LOCKSTEP_EXECUTABLE", executable.str()});
    options.captureOutput = false;
    return eacp::Processes::run(options).exitCode;
}

int runCommand(lockstep::CommandOptions options)
{
    options.cache = getCacheDirectory(options.cache);

    if (options.command == "resolve")
        return lockstep::runResolveCommand(options);

    if (options.command == "update")
        return lockstep::runUpdateCommand(options);

    if (options.command == "verify")
        return lockstep::runVerifyCommand(options);

    if (options.command == "root")
        return lockstep::runRootCommand(options);

    throw std::runtime_error("unknown command '" + options.command
                             + "'; run lockstep --help for the list");
}

int parseAndRunCommand()
{
    if (auto exitCode = runProjectPinnedVersionInstead())
        return *exitCode;

    auto options = lockstep::CommandOptions {};
    auto cli = eacp::Args::Parser {"lockstep", toolDescription};

    cli.positional(options.command, "command", "resolve, update, verify or root");
    cli.positional(options.names, "name", "packages for update (all when none)");
    cli.option("--name", options.name, "the package to resolve");
    cli.option("--kind", options.kind, "git or archive").choices({"git", "archive"});
    cli.option("--url", options.url, "the git repository or archive URL");
    cli.option("--ref", options.ref, "the branch, tag or commit to resolve");
    cli.option(
        "--patch", options.patches, "a patch applied with git apply, repeatable");
    cli.option("--requester", options.requester, "who asked, named in errors");
    cli.option("--mode", options.mode, "locked, update or offline")
        .choices({"locked", "update", "offline"});
    cli.option("--lock", options.lock, "the lock file to read and write");
    cli.option("--cache",
               options.cache,
               "the shared cache; else $LOCKSTEP_CACHE, else the user cache folder");
    cli.option("--shim", options.shim, "the lockstep.cmake to record in the lock");
    cli.option("--cmake", options.cmake, "the cmake that extracts tar archives");
    cli.flag("--full",
             options.full,
             "verify rehashes every tree instead of trusting stamps");
    cli.epilogue(commandsHelp);

    if (auto code = cli.exitCodeFor(cli.parse()))
        return *code;

    return runCommand(options);
}
} // namespace

int main(int argc, char* argv[])
{
    return eacp::Apps::runConsole(argc, argv, parseAndRunCommand);
}
