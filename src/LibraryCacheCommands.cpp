#include "CMakeSnippet.h"
#include "Commands.h"
#include "LibraryCache.h"
#include "LibraryCacheLocation.h"
#include "LibraryStore.h"
#include "Messages.h"
#include "PackageArguments.h"
#include "Session.h"
#include "Status.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Format.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace lockstep
{
namespace
{
constexpr auto bytesPerMebibyte = 1024.0 * 1024.0;
constexpr auto statisticsLabelWidth = 14;
constexpr auto statisticsValueWidth = 10;
constexpr auto millisecondsPerSecond = 1000.0;
constexpr auto sendLogName = "send.log";
constexpr auto sendFailureName = "send-failure.txt";
constexpr auto cacheSubcommandsHelp = std::string_view {
    "  lockstep cache stats   print the stored libraries, their size, hits, "
    "misses and stores\n"
    "  lockstep cache clear   delete the stored libraries and reset the counts"};
constexpr auto stubDeclaration =
    std::string_view {"typedef int lockstep_library_cache_stub;\n"};
constexpr auto stubFileNames =
    std::array<std::pair<std::string_view, std::string_view>, 2> {
        {{"CXX", "stub.cpp"}, {"C", "stub.c"}}};
constexpr auto linkingTargetTypes = std::array<std::string_view, 3> {
    "EXECUTABLE", "SHARED_LIBRARY", "MODULE_LIBRARY"};

struct LibraryDecision
{
    std::string target;
    LibraryKey key;
    std::optional<eacp::FilePath> cachedLibrary;
    bool isFetched = false;
};

struct SkippedLibrary
{
    std::string target;
    std::string reason;
};

struct LibraryCachePlan
{
    std::vector<LibraryDecision> libraries;
    std::vector<SkippedLibrary> skipped;
};

eacp::FilePath getLibraryCacheDirectory(const eacp::FilePath& buildDirectory)
{
    return buildDirectory / "_lockstep" / "library-cache";
}

eacp::FilePath getInputsPath(const eacp::FilePath& buildDirectory,
                             const std::string& key)
{
    return getLibraryCacheDirectory(buildDirectory) / "inputs" / (key + ".txt");
}

eacp::FilePath getStubPath(const eacp::FilePath& buildDirectory,
                           const std::string& language)
{
    for (const auto& [stubLanguage, fileName]: stubFileNames)
    {
        if (stubLanguage == language)
            return getLibraryCacheDirectory(buildDirectory) / fileName;
    }

    throw std::runtime_error("the library cache has no stub source for " + language);
}

// Rewritten only when it changes, so no stub object is rebuilt for nothing.
void writeFileIfChanged(const eacp::FilePath& path, std::string_view text)
{
    if (eacp::Files::isFile(path) && eacp::Files::readFile(path) == text)
        return;

    eacp::Files::createDirectories(path.parentDirectory());
    eacp::Files::writeFile(path, text);
}

eacp::FilePath getSendLogPath(const eacp::FilePath& buildDirectory)
{
    return getLibraryCacheDirectory(buildDirectory) / sendLogName;
}

eacp::FilePath getSendFailurePath(const eacp::FilePath& buildDirectory)
{
    return getLibraryCacheDirectory(buildDirectory) / sendFailureName;
}

std::string quoteForShell(const std::string& text)
{
    return "\"" + text + "\"";
}

std::vector<ConfiguredPackage> findConfiguredPackages(const CommandOptions& options)
{
    auto cacheSources = toForwardSlashes((options.cache / "src").str()) + "/";
    auto packages = std::vector<ConfiguredPackage> {};

    for (const auto& package: readSession(options.buildDirectory).packages)
    {
        auto directory = toForwardSlashes(package.sourceDirectory);
        auto hash = std::string {};

        if (directory.starts_with(cacheSources)
            && directory.find('/', cacheSources.size()) == std::string::npos)
            hash = directory.substr(cacheSources.size());

        packages.push_back({package.name, hash, directory});
    }

    return packages;
}

PackageVariableNames collectVariableNamesOfCachedPackages(
    const std::vector<ConfiguredPackage>& packages)
{
    auto names = PackageVariableNames {};

    for (const auto& package: packages)
    {
        if (!package.hash.empty())
            names[package.hash] = collectPackageVariableNames(
                eacp::FilePath {package.sourceDirectory});
    }

    return names;
}

bool isPackageStaticLibrary(const LibraryCacheInputs& inputs,
                            const RecordFields& fields)
{
    return findField(fields, "TYPE") == "STATIC_LIBRARY"
           && !isCMakeTrue(findField(fields, "IMPORTED"))
           && findOwningPackage(inputs.packages, findField(fields, "SOURCE_DIR"));
}

LibraryCachePlan planLibraryCache(const LibraryCacheInputs& inputs,
                                  const LibraryStore& store)
{
    auto plan = LibraryCachePlan {};

    for (const auto& [target, fields]: inputs.description.targets)
    {
        if (!isPackageStaticLibrary(inputs, fields))
            continue;

        if (auto reason = findReasonNotToCache(inputs, target))
        {
            plan.skipped.push_back({target, *reason});
            continue;
        }

        auto key = computeLibraryKey(inputs, target);
        plan.libraries.push_back({target, key, store.findLibrary(key.key)});
    }

    return plan;
}

std::string getLockstepExecutable()
{
    auto executable = eacp::getEnvValue("LOCKSTEP_EXECUTABLE");

    if (executable.empty())
        throw std::runtime_error(
            "the library cache runs from the CMake module, which sets "
            "LOCKSTEP_EXECUTABLE");

    return executable;
}

// The options every library cache command the build runs takes.
eacp::Vector<std::string> createSharedArguments(const CommandOptions& options)
{
    auto arguments =
        eacp::Vector<std::string> {"--cache=" + options.cache.str(),
                                   "--build-dir=" + options.buildDirectory.str(),
                                   "--cmake=" + options.cmake.str()};

    if (!options.libraryCacheLocation.empty())
        arguments.add("--library-cache-location=" + options.libraryCacheLocation);

    return arguments;
}

std::string createLauncher(const CommandOptions& options, std::string_view command)
{
    auto executable = getLockstepExecutable();
    auto launcher = quoteForShell(options.cmake.str()) + " -E env "
                    + quoteForShell("LOCKSTEP_EXECUTABLE=" + executable) + " "
                    + quoteForShell(executable);

    for (const auto& argument: createSharedArguments(options))
        launcher += " " + quoteForShell(argument);

    return launcher + " " + std::string {command};
}

// Sending runs in a process of its own that outlives the link or configure that
// started it, so neither waits on the network.
void startSendingLibraries(const CommandOptions& options,
                           const std::vector<std::string>& keys)
{
    if (keys.empty() || options.libraryCacheLocation.empty())
        return;

    auto executable = getLockstepExecutable();
    auto process = eacp::Processes::ProcessOptions {};
    process.executable = executable;
    process.arguments = {"send-libraries"};
    process.arguments.addFrom(createSharedArguments(options));

    for (const auto& key: keys)
        process.arguments.add("--library=" + key);

    process.environment.add({"LOCKSTEP_EXECUTABLE", executable});
    process.workingDirectory = eacp::FilePath::tempDirectory().str();
    process.detached = true;
    eacp::Processes::Process {process};
}

std::string createPropertySetting(const std::string& target,
                                  std::string_view property,
                                  const std::string& value)
{
    return "set_property(TARGET " + quoteForCMake(target) + " PROPERTY "
           + std::string {property} + " " + value + ")\n";
}

// The target compiles one empty stub instead of its sources, and its archive
// step copies the cached library into place instead of archiving the stub.
std::string createHitSnippet(const CommandOptions& options,
                             const ConfigureDescription& description,
                             const LibraryDecision& decision)
{
    const auto& fields = description.targets.at(decision.target);
    auto kept = findField(fields, "KEPT_SOURCES");
    auto stub =
        getStubPath(options.buildDirectory, findField(fields, "STUB_LANGUAGE"));
    auto sources = quoteForCMake(stub.str());

    if (!kept.empty())
        sources = quoteForCMake(kept) + " " + sources;

    auto launcher =
        createLauncher(options, "use-cached-library") + " "
        + quoteForShell("--library=" + decision.cachedLibrary->str()) + " "
        + quoteForShell("--output=$<TARGET_FILE:" + decision.target + ">") + " --";

    return createPropertySetting(decision.target, "SOURCES", sources)
           + createPropertySetting(
               decision.target, "RULE_LAUNCH_LINK", quoteForCMake(launcher));
}

bool isLinkingTarget(const RecordFields& fields)
{
    auto type = findField(fields, "TYPE");
    return std::ranges::find(linkingTargetTypes, type) != linkingTargetTypes.end()
           && !isCMakeTrue(findField(fields, "IMPORTED"));
}

// Every executable or shared library that links a missed library stores it
// once its own link succeeds, so only what a build really linked is stored.
std::string createStoringSnippet(const CommandOptions& options,
                                 const ConfigureDescription& description,
                                 const LibraryCachePlan& plan)
{
    auto snippet = std::string {};

    for (const auto& [target, fields]: description.targets)
    {
        if (!isLinkingTarget(fields))
            continue;

        auto closure = collectLinkClosure(description, target);
        auto libraries = std::string {};

        for (const auto& decision: plan.libraries)
        {
            if (!decision.cachedLibrary && closure.contains(decision.target))
                libraries +=
                    " "
                    + quoteForShell("--library=" + decision.key.key
                                    + "=$<TARGET_FILE:" + decision.target + ">");
        }

        if (libraries.empty())
            continue;

        auto launcher = createLauncher(options, "store-libraries") + libraries
                        + " -- " + findField(fields, "EXISTING_LINK_LAUNCHER");
        snippet += createPropertySetting(
            target, "RULE_LAUNCH_LINK", quoteForCMake(launcher));
    }

    return snippet;
}

std::string createMessage(std::string_view level, const std::string& text)
{
    return "message(" + std::string {level} + " "
           + quoteForCMake("lockstep: " + text) + ")\n";
}

LibraryCacheStatistics countHitsAndMisses(const LibraryCachePlan& plan)
{
    auto counts = LibraryCacheStatistics {};

    for (const auto& decision: plan.libraries)
    {
        if (decision.cachedLibrary)
            ++counts.hits;
        else
            ++counts.misses;

        if (decision.isFetched)
            ++counts.fetchedHits;
    }

    return counts;
}

std::string describeSeconds(std::chrono::steady_clock::duration duration)
{
    auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    auto seconds = static_cast<double>(milliseconds) / millisecondsPerSecond;
    return eacp::Strings::format("{:.1f} s", seconds);
}

LibraryFetchRequest createFetchRequest(const LibraryCachePlan& plan)
{
    auto request = LibraryFetchRequest {};

    for (const auto& decision: plan.libraries)
    {
        if (decision.cachedLibrary)
            request.copiedKeys.push_back(decision.key.key);
        else
            request.missingKeys.push_back(decision.key.key);
    }

    return request;
}

void markFetchedLibraries(LibraryCachePlan& plan,
                          const LibraryFetchResult& fetched,
                          const LibraryStore& store)
{
    for (auto& decision: plan.libraries)
    {
        if (!fetched.fetchedKeys.contains(decision.key.key))
            continue;

        decision.cachedLibrary = store.findLibrary(decision.key.key);
        decision.isFetched = decision.cachedLibrary.has_value();
    }
}

std::string
    describeDiscardedLibraries(const LibraryCacheLocation& location,
                               const std::vector<DiscardedLibrary>& discarded)
{
    auto descriptions = eacp::Vector<std::string> {};

    for (const auto& library: discarded)
        descriptions.add(library.key + " (" + library.reason + ")");

    return "discarded " + std::to_string(discarded.size())
           + " libraries fetched from " + location.text
           + ", which build from source instead: "
           + eacp::Strings::join(descriptions, "; ");
}

// A send after an earlier build failed: said once, here, since the build it
// ran after had already finished.
std::string reportEarlierSendFailure(const CommandOptions& options)
{
    auto failurePath = getSendFailurePath(options.buildDirectory);

    if (!eacp::Files::isFile(failurePath))
        return {};

    auto reason = eacp::Strings::trim(eacp::Files::readFile(failurePath));
    eacp::Files::removeFile(failurePath);
    return createMessage("WARNING",
                         "sending libraries to " + options.libraryCacheLocation
                             + " after an earlier build failed: " + reason
                             + ". This configure sends the ones it still lacks.");
}

// Fetches the libraries this machine has no copy of from the library cache
// location, and starts sending the ones the location lacks. Returns the CMake
// that reports it.
std::string fetchFromLibraryCacheLocation(const CommandOptions& options,
                                          LibraryCachePlan& plan,
                                          const LibraryStore& store)
{
    auto location = parseLibraryCacheLocation(options.libraryCacheLocation);

    if (!location)
        return {};

    auto report = reportEarlierSendFailure(options);
    auto request = createFetchRequest(plan);
    auto started = std::chrono::steady_clock::now();

    try
    {
        auto fetched =
            LibraryCacheAtLocation {*location, options.cmake}.fetchLibraries(request,
                                                                             store);
        markFetchedLibraries(plan, fetched, store);

        if (!fetched.discarded.empty())
            report += createMessage(
                "WARNING", describeDiscardedLibraries(*location, fetched.discarded));

        startSendingLibraries(options, fetched.keysLocationLacks);
        return report
               + createMessage(
                   "STATUS",
                   "library cache at " + location->text + ": fetched "
                       + std::to_string(fetched.fetchedKeys.size()) + " of the "
                       + std::to_string(request.missingKeys.size())
                       + " libraries this machine had no copy of in "
                       + describeSeconds(std::chrono::steady_clock::now() - started)
                       + "; sending "
                       + std::to_string(fetched.keysLocationLacks.size())
                       + " it lacks");
    }
    catch (const std::exception& error)
    {
        return report
               + createMessage("WARNING",
                               "cannot use the library cache at " + location->text
                                   + ", so this configure uses this machine's "
                                     "copies only: "
                                   + error.what());
    }
}

std::string createReport(const LibraryCachePlan& plan)
{
    auto report = std::string {};

    for (const auto& decision: plan.libraries)
    {
        if (decision.cachedLibrary)
        {
            auto source =
                decision.isFetched ? std::string {", fetched"} : std::string {};
            report += createMessage("VERBOSE",
                                    "library cache hit for " + decision.target + " ("
                                        + decision.key.key + source + ")");
        }
        else
        {
            report += createMessage(
                "VERBOSE",
                "library cache miss for " + decision.target + " (" + decision.key.key
                    + "); the first link that uses it stores it");
        }
    }

    for (const auto& skipped: plan.skipped)
        report += createMessage("VERBOSE",
                                "library cache skips " + skipped.target + ": "
                                    + skipped.reason);

    auto counts = countHitsAndMisses(plan);
    return report
           + createMessage("STATUS",
                           "library cache: " + std::to_string(counts.hits)
                               + " hits, " + std::to_string(counts.misses)
                               + " misses, " + std::to_string(plan.skipped.size())
                               + " skipped (--log-level=VERBOSE names them)");
}

void writeLibraryCacheFiles(const CommandOptions& options,
                            const LibraryCachePlan& plan)
{
    for (const auto& [language, fileName]: stubFileNames)
        writeFileIfChanged(getLibraryCacheDirectory(options.buildDirectory)
                               / fileName,
                           stubDeclaration);

    for (const auto& decision: plan.libraries)
        writeFileIfChanged(getInputsPath(options.buildDirectory, decision.key.key),
                           decision.key.inputs);
}

void addToStatisticsOrWarn(const LibraryStore& store,
                           const LibraryCacheStatistics& added)
{
    try
    {
        store.addToStatistics(added);
    }
    catch (const std::exception& error)
    {
        printStatus(std::string {"WARNING: could not count in the library cache: "}
                    + error.what());
    }
}

void storeLinkedLibraries(const CommandOptions& options,
                          const std::vector<std::string>& command)
{
    auto store = LibraryStore {options.cache};
    auto workingDirectory = eacp::FilePath {std::filesystem::current_path()};
    auto stored = LibraryCacheStatistics {};
    auto storedKeys = std::vector<std::string> {};

    for (const auto& entry: options.libraries)
    {
        if (entry.size() <= libraryKeyLength || entry[libraryKeyLength] != '=')
            throw std::runtime_error(
                "store-libraries takes --library=<key>=<library>, not " + entry);

        auto key = entry.substr(0, libraryKeyLength);
        auto library = eacp::FilePath {entry.substr(libraryKeyLength + 1)};

        if (!isCommandInput(command, library, workingDirectory))
            continue;

        try
        {
            auto inputs =
                eacp::Files::readFile(getInputsPath(options.buildDirectory, key));

            if (store.storeLibrary(key, library, inputs))
            {
                ++stored.stores;
                storedKeys.push_back(key);
            }
        }
        catch (const std::exception& error)
        {
            printStatus("WARNING: could not store " + library.str()
                        + " in the library cache: " + error.what());
        }
    }

    if (stored.stores > 0)
        addToStatisticsOrWarn(store, stored);

    startSendingLibraries(options, storedKeys);
}

std::string formatMebibytes(std::uint64_t byteCount)
{
    auto mebibytes = static_cast<double>(byteCount) / bytesPerMebibyte;
    return eacp::Strings::format("{:.1f} MiB", mebibytes);
}

std::string describeWrongCacheSubcommand(const eacp::Vector<std::string>& names)
{
    if (names.empty())
        return "cache needs a subcommand";

    return "'cache " + eacp::Strings::join(names, " ") + "' is not a cache command";
}

struct StatisticsLine
{
    std::string label;
    std::string value;
    std::string meaning;
};

std::vector<StatisticsLine> createStatisticsLines(const LibraryStoreSummary& summary,
                                                  bool hasLocation)
{
    const auto& statistics = summary.statistics;
    auto missesNotStored = std::max(0, statistics.misses - statistics.stores);
    auto lines = std::vector<StatisticsLine> {
        {"libraries", std::to_string(summary.libraryCount), "stored now"},
        {"size", formatMebibytes(summary.byteCount), "on disk"},
        {"hits",
         std::to_string(statistics.hits),
         "a configure found the library stored, so it was not built"}};

    if (hasLocation || statistics.fetchedHits > 0)
        lines.push_back({"  fetched",
                         std::to_string(statistics.fetchedHits),
                         "copied from the library cache location, which had it"});

    lines.insert(
        lines.end(),
        {{"misses",
          std::to_string(statistics.misses),
          "a configure did not find the library stored:"},
         {"  stored",
          std::to_string(statistics.stores),
          "built, then stored by the first link that used it"},
         {"  not stored",
          std::to_string(missesNotStored),
          "no link used it: the project does not build it, or has not built yet"}});
    return lines;
}

void printStoreSummary(const CommandOptions& options)
{
    auto copies = (options.cache / "lib").str();
    auto hasLocation = !options.libraryCacheLocation.empty();

    if (hasLocation)
        eacp::Console::print("library cache at " + options.libraryCacheLocation
                             + "; this machine's copies in " + copies);
    else
        eacp::Console::print("library cache " + copies);

    for (const auto& line: createStatisticsLines(
             LibraryStore {options.cache}.summarize(), hasLocation))
    {
        eacp::Console::print(eacp::Strings::format("  {:<{}}{:<{}}{}",
                                                   line.label,
                                                   statisticsLabelWidth,
                                                   line.value,
                                                   statisticsValueWidth,
                                                   line.meaning));
    }
}
std::string describeClearedStore(const CommandOptions& options)
{
    auto copies = (options.cache / "lib").str();

    if (options.libraryCacheLocation.empty())
        return "deleted " + copies;

    return "deleted this machine's copies in " + copies
           + " and their counts; the library cache at "
           + options.libraryCacheLocation + " is left as it is";
}
} // namespace

int runLibraryCacheCommand(const CommandOptions& options)
{
    auto description =
        parseConfigureDescription(eacp::Files::readFile(options.input));
    auto packages = findConfiguredPackages(options);
    auto variableNames = collectVariableNamesOfCachedPackages(packages);
    auto inputs =
        LibraryCacheInputs {description,
                            packages,
                            variableNames,
                            {toForwardSlashes(options.buildDirectory.str()),
                             toForwardSlashes(options.cache.str())}};
    auto store = LibraryStore {options.cache};
    auto plan = planLibraryCache(inputs, store);
    auto locationReport = fetchFromLibraryCacheLocation(options, plan, store);
    addToStatisticsOrWarn(store, countHitsAndMisses(plan));
    auto snippet = std::string {};

    for (const auto& decision: plan.libraries)
    {
        if (decision.cachedLibrary)
            snippet += createHitSnippet(options, description, decision);
    }

    snippet += createStoringSnippet(options, description, plan);
    writeLibraryCacheFiles(options, plan);
    eacp::Files::createDirectories(options.output.parentDirectory());
    eacp::Files::writeFile(
        options.output,
        std::string_view {snippet + locationReport + createReport(plan)});
    return 0;
}

int runUseCachedLibraryCommand(const CommandOptions& options)
{
    if (options.libraries.size() != 1)
        throw std::runtime_error("use-cached-library takes one --library");

    auto library = eacp::FilePath {options.libraries[0]};

    if (!eacp::Files::isFile(library))
    {
        throw std::runtime_error(formatUserError(
            "the cached library " + library.str() + " is gone from the cache.",
            "Configure again, and lockstep builds it from source."));
    }

    if (!eacp::Files::copyFile(library, options.output))
        throw std::runtime_error("cannot copy " + library.str() + " to "
                                 + options.output.str());

    return 0;
}

int runStoreLibrariesCommand(const CommandOptions& options)
{
    const auto& command = options.argumentsAfterSeparator;

    if (command.empty())
        throw std::runtime_error(
            "store-libraries runs the command after --, and none "
            "was given");

    auto process = eacp::Processes::ProcessOptions {};
    process.executable = command.front();
    process.arguments.getVector().assign(command.begin() + 1, command.end());
    process.captureOutput = false;
    auto result = eacp::Processes::run(process);

    if (!result.launched)
        throw std::runtime_error("store-libraries could not run " + command.front());

    if (result.exitCode != 0)
        return result.exitCode;

    storeLinkedLibraries(options, command);
    return 0;
}

int runSendLibrariesCommand(const CommandOptions& options)
{
    auto location = parseLibraryCacheLocation(options.libraryCacheLocation);

    if (!location)
        throw std::runtime_error("send-libraries needs --library-cache-location");

    // Started detached: output goes to a log rather than to the build's pipe,
    // which the build tool would wait on until this process ends.
    auto log = getSendLogPath(options.buildDirectory);
    eacp::Files::createDirectories(log.parentDirectory());
    std::freopen(log.str().c_str(), "a", stdout);
    std::freopen(log.str().c_str(), "a", stderr);
    auto keys = std::vector<std::string> {options.libraries.begin(),
                                          options.libraries.end()};

    try
    {
        LibraryCacheAtLocation {*location, options.cmake}.sendLibraries(
            keys, LibraryStore {options.cache});
        printStatus("sent " + std::to_string(keys.size()) + " libraries to "
                    + location->text);
        return 0;
    }
    catch (const std::exception& error)
    {
        eacp::Files::writeFile(getSendFailurePath(options.buildDirectory),
                               std::string_view {error.what()});
        printStatus("could not send " + std::to_string(keys.size())
                    + " libraries to " + location->text + ": " + error.what());
        return 1;
    }
}

int runCacheCommand(const CommandOptions& options)
{
    auto subcommand = options.names.empty() ? std::string {} : options.names[0];

    if (subcommand == "stats" && options.names.size() == 1)
    {
        printStoreSummary(options);
        return 0;
    }

    if (subcommand == "clear" && options.names.size() == 1)
    {
        LibraryStore {options.cache}.clear();
        eacp::Console::print(describeClearedStore(options));
        return 0;
    }

    throw std::runtime_error(describeWrongCacheSubcommand(options.names)
                             + "; use one of:\n"
                             + std::string {cacheSubcommandsHelp});
}
} // namespace lockstep
