#include "ByteSize.h"
#include "CMakeSnippet.h"
#include "Commands.h"
#include "CompilerExecutable.h"
#include "LibraryCacheSettings.h"
#include "LibraryEviction.h"
#include "LibraryCache.h"
#include "LibraryCacheLocation.h"
#include "LibraryCachePlan.h"
#include "LongPath.h"
#include "LibraryStore.h"
#include "Messages.h"
#include "PinnedRelease.h"
#include "PackageArguments.h"
#include "ProjectSettings.h"
#include "Session.h"
#include "Status.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Format.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <Miro/Json.h>

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
constexpr auto statisticsLabelGap = 2;
constexpr auto statisticsValueWidth = 10;
constexpr auto millisecondsPerSecond = 1000.0;
constexpr auto sendLogName = "send.log";
constexpr auto sendFailureName = "send-failure.txt";

#if defined(_WIN32)
constexpr auto arePathsCaseInsensitive = true;
#else
constexpr auto arePathsCaseInsensitive = false;
#endif
constexpr auto cacheSubcommandsHelp = std::string_view {
    "  lockstep cache stats   print the stored libraries, their size, hits, "
    "misses and stores\n"
    "  lockstep cache clear   delete the stored libraries and reset the counts"};

eacp::FilePath getSendLogPath(const eacp::FilePath& buildDirectory)
{
    return getLibraryCacheDirectory(buildDirectory) / sendLogName;
}

eacp::FilePath getSendFailurePath(const eacp::FilePath& buildDirectory)
{
    return getLibraryCacheDirectory(buildDirectory) / sendFailureName;
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

// Sending runs in a process of its own that outlives the link or configure that
// started it, so neither waits on the network.
void startSendingLibraries(const CommandOptions& options,
                           const std::vector<EntryAddress>& addresses)
{
    if (addresses.empty() || options.libraryCacheLocation.empty())
        return;

    auto executable = getLockstepExecutable();
    auto process = eacp::Processes::ProcessOptions {};
    process.executable = executable;
    process.arguments = {"send-libraries"};
    process.arguments.addFrom(createSharedArguments(options));

    process.arguments.addFrom(createSizeLimitArguments(options));

    for (const auto& address: addresses)
        process.arguments.add("--library=" + address.getPath());

    process.environment.add({"LOCKSTEP_EXECUTABLE", executable});
    process.workingDirectory = eacp::FilePath::tempDirectory().str();
    process.detached = true;
    eacp::Processes::Process {process};
}

std::string describeSeconds(std::chrono::steady_clock::duration duration)
{
    auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    auto seconds = static_cast<double>(milliseconds) / millisecondsPerSecond;
    return eacp::Strings::format("{:.1f} s", seconds);
}

LibraryFetchRequest createFetchRequest(const LibraryCachePlan& plan,
                                       const ToolchainRanges& ranges)
{
    auto request = LibraryFetchRequest {};
    request.fetchesOtherBuilds = ranges.isAnySet();

    for (const auto& decision: plan.decisions)
    {
        if (decision.cached)
            request.copied.push_back(decision.cached->address);
        else
            request.missing.push_back({decision.key.key, decision.key.buildId});
    }

    return request;
}

void markFetchedLibraries(LibraryCachePlan& plan, const LibraryFetchResult& fetched)
{
    for (auto& decision: plan.decisions)
        decision.isFetched =
            decision.cached && fetched.fetched.contains(decision.cached->address);
}

std::string
    describeDiscardedLibraries(const LibraryCacheLocation& location,
                               const std::vector<DiscardedLibrary>& discarded,
                               const std::string& entries)
{
    auto descriptions = eacp::Vector<std::string> {};

    for (const auto& library: discarded)
        descriptions.add(library.address.getPath() + " (" + library.reason + ")");

    return "discarded " + std::to_string(discarded.size()) + " " + entries
           + " fetched from " + location.text + ", which build from source instead: "
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

int countFetchedHits(const LibraryCachePlan& plan)
{
    auto isFetched = [](const LibraryDecision& decision)
    { return decision.isFetched; };
    return static_cast<int>(std::ranges::count_if(plan.decisions, isFetched));
}

// Fetches the libraries this machine has no copy of from the library cache
// location, plans again with what came, and starts sending the ones the
// location lacks. Returns the CMake that reports it.
std::string fetchFromLibraryCacheLocation(const CommandOptions& options,
                                          LibraryCachePlan& plan,
                                          const LibraryCachePlanning& planning)
{
    auto location = parseLibraryCacheLocation(options.libraryCacheLocation);

    if (!location)
        return {};

    auto report = reportEarlierSendFailure(options);
    auto request = createFetchRequest(plan, planning.ranges);
    auto entries = describeEntriesInPlural(plan);
    auto started = std::chrono::steady_clock::now();

    try
    {
        auto fetched =
            LibraryCacheAtLocation {*location, options.cmake}.fetchLibraries(
                request, planning.store);
        plan = planLibraryCache(planning);
        markFetchedLibraries(plan, fetched);

        if (!fetched.discarded.empty())
            report += createMessage(
                "WARNING",
                describeDiscardedLibraries(*location, fetched.discarded, entries));

        startSendingLibraries(options, fetched.locationLacks);
        return report
               + createMessage(
                   "STATUS",
                   "library cache at " + location->text + ": fetched "
                       + std::to_string(countFetchedHits(plan)) + " of the "
                       + std::to_string(request.missing.size()) + " " + entries
                       + " this machine had no copy of in "
                       + describeSeconds(std::chrono::steady_clock::now() - started)
                       + "; sending " + std::to_string(fetched.locationLacks.size())
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

// The limits the configure passed, or the defaults for a build directory a
// configure set up without them.
LibraryCacheSizeLimits getSizeLimits(const CommandOptions& options)
{
    auto limits = parseSizeLimits(resolveLibraryCacheSettings({}));

    if (!options.libraryCacheMaxSize.empty())
        limits.localBytes = parseByteSize(options.libraryCacheMaxSize);

    if (!options.libraryCacheLocationMaxSize.empty())
        limits.locationBytes = parseByteSize(options.libraryCacheLocationMaxSize);

    return limits;
}

std::vector<std::string> readStoredObjectList(const eacp::FilePath& list)
{
    auto objects = std::vector<std::string> {};

    if (!eacp::Files::isFile(list))
        return objects;

    for (const auto& line: eacp::Strings::split(eacp::Files::readFile(list), '\n'))
    {
        auto object = eacp::Strings::trim(line);

        if (!object.empty())
            objects.push_back(object);
    }

    return objects;
}

// A copy out of <store>/<key>/<build id>/ marks that entry used.
void markStoredFileUsed(const CommandOptions& options, const eacp::FilePath& stored)
{
    auto build = stored.parentDirectory();
    auto address = parseEntryAddress(build.parentDirectory().fileName() + "/"
                                     + build.fileName());

    if (address)
        LibraryStore {options.cache}.markUsed(*address);
}

void markHitsUsed(const LibraryStore& store, const LibraryCachePlan& plan)
{
    for (const auto& decision: plan.decisions)
    {
        if (decision.cached)
            store.markUsed(decision.cached->address);
    }
}

// Evicts what the limit says and counts it. Returns the line that says so, or
// nothing when nothing went.
std::string evictFromStore(const LibraryStore& store, std::uint64_t limitBytes)
{
    auto result = evictLeastRecentlyUsed(store, {limitBytes});

    if (result.evictedCount == 0)
        return {};

    auto evicted = LibraryCacheStatistics {};
    evicted.evictions = result.evictedCount;
    addToStatisticsOrWarn(store, evicted);
    return "library cache: deleted the " + std::to_string(result.evictedCount)
           + " least recently used entries to stay under its "
           + describeByteSize(limitBytes) + " limit";
}

void countLocationEvictions(const LibraryStore& store, int evictedCount)
{
    if (evictedCount == 0)
        return;

    auto evicted = LibraryCacheStatistics {};
    evicted.locationEvictions = evictedCount;
    addToStatisticsOrWarn(store, evicted);
}

// A --library, --executable or --object-library value: <key>=<what to store>.
struct KeyedValue
{
    std::string key;
    std::string value;
};

KeyedValue splitKeyedValue(const std::string& text, std::string_view option)
{
    if (text.size() <= libraryKeyLength || text[libraryKeyLength] != '=')
        throw std::runtime_error("store-libraries takes " + std::string {option}
                                 + "=<key>=<file>, not " + text);

    return {text.substr(0, libraryKeyLength), text.substr(libraryKeyLength + 1)};
}

struct EntryFromLink
{
    std::string key;
    EntryToStore entry;
};

BuiltWith readBuiltWith(const eacp::FilePath& buildDirectory, const std::string& key)
{
    auto path = getBuiltWithPath(buildDirectory, key);
    return Miro::createFromJSONString<BuiltWith>(eacp::Files::readFile(path));
}

std::vector<eacp::FilePath> toFilePaths(const std::vector<std::string>& paths)
{
    auto filePaths = std::vector<eacp::FilePath> {};

    for (const auto& path: paths)
        filePaths.emplace_back(path);

    return filePaths;
}

// Whether a link command read a file, as a path or through a response file.
class LinkCommandInputs
{
public:
    explicit LinkCommandInputs(const std::vector<std::string>& commandToRead)
        : command(commandToRead)
        , workingDirectory(std::filesystem::current_path())
    {
    }

    bool isRead(const eacp::FilePath& file) const
    {
        return isCommandInput(command, file, workingDirectory);
    }

private:
    const std::vector<std::string>& command;
    eacp::FilePath workingDirectory;
};

void addLinkedLibraries(std::vector<EntryFromLink>& entries,
                        const CommandOptions& options,
                        const LinkCommandInputs& inputs)
{
    for (const auto& text: options.libraries)
    {
        auto [key, path] = splitKeyedValue(text, "--library");
        auto library = eacp::FilePath {path};

        if (inputs.isRead(library))
            entries.push_back(
                {key, {CachedTargetKind::staticLibrary, {library}, {}}});
    }
}

void addLinkedExecutables(std::vector<EntryFromLink>& entries,
                          const CommandOptions& options)
{
    for (const auto& text: options.executables)
    {
        auto [key, path] = splitKeyedValue(text, "--executable");
        entries.push_back(
            {key, {CachedTargetKind::executable, {eacp::FilePath {path}}, {}}});
    }
}

// An object library is stored only when the link read every one of its
// objects, so each was built for this link.
void addLinkedObjectLibraries(std::vector<EntryFromLink>& entries,
                              const CommandOptions& options,
                              const LinkCommandInputs& inputs)
{
    auto isRead = [&](const eacp::FilePath& object)
    { return inputs.isRead(object); };

    for (const auto& text: options.objectLibraries)
    {
        auto [key, list] = splitKeyedValue(text, "--object-library");
        auto objects = toFilePaths(splitCMakeList(list));

        if (!objects.empty() && std::ranges::all_of(objects, isRead))
            entries.push_back({key, {CachedTargetKind::objectLibrary, objects, {}}});
    }
}

// What a link that succeeded stores: the libraries and object files its
// command read, and the executable it wrote.
std::vector<EntryFromLink>
    collectEntriesFromLink(const CommandOptions& options,
                           const std::vector<std::string>& command)
{
    auto inputs = LinkCommandInputs {command};
    auto entries = std::vector<EntryFromLink> {};
    addLinkedLibraries(entries, options, inputs);
    addLinkedExecutables(entries, options);
    addLinkedObjectLibraries(entries, options, inputs);
    return entries;
}

void storeLinkedEntries(const CommandOptions& options,
                        const std::vector<std::string>& command)
{
    auto store = LibraryStore {options.cache};
    auto stored = LibraryCacheStatistics {};
    auto storedAddresses = std::vector<EntryAddress> {};

    for (auto& [key, entry]: collectEntriesFromLink(options, command))
    {
        try
        {
            entry.inputs =
                eacp::Files::readFile(getInputsPath(options.buildDirectory, key));
            entry.builtWith = readBuiltWith(options.buildDirectory, key);

            if (store.storeEntry(key, entry))
            {
                ++stored.getCounts(entry.kind).stores;
                storedAddresses.push_back({key, computeBuildId(entry.builtWith)});
            }
        }
        catch (const std::exception& error)
        {
            printStatus("WARNING: could not store " + entry.files.front().str()
                        + " in the library cache: " + error.what());
        }
    }

    if (!storedAddresses.empty())
        addToStatisticsOrWarn(store, stored);

    if (auto evicted = evictFromStore(store, getSizeLimits(options).localBytes);
        !evicted.empty())
        printStatus(evicted);
    startSendingLibraries(options, storedAddresses);
}

int runWrappedCommand(const std::vector<std::string>& command,
                      std::string_view runner)
{
    if (command.empty())
        throw std::runtime_error(std::string {runner}
                                 + " runs the command after --, and none was given");

    auto process = eacp::Processes::ProcessOptions {};
    process.executable = command.front();
    process.arguments.getVector().assign(command.begin() + 1, command.end());
    process.captureOutput = false;
    auto result = eacp::Processes::run(process);

    if (!result.launched)
        throw std::runtime_error(std::string {runner} + " could not run "
                                 + command.front());

    return result.exitCode;
}

// Copies beside `destination` and renames the copy over it, so a file that ran
// before is replaced rather than rewritten in place: macOS kills a process
// whose signed executable changed under it.
void copyIntoPlace(const eacp::FilePath& stored, const eacp::FilePath& destination)
{
    if (!eacp::Files::isFile(stored))
    {
        throw std::runtime_error(formatUserError(
            "the cached file " + stored.str() + " is gone from the cache.",
            "Configure again, and lockstep builds it from source."));
    }

    eacp::Files::createDirectories(destination.parentDirectory());
    auto copy =
        eacp::TemporaryFile {destination.parentDirectory(), ".lockstep-copy"};

    if (!eacp::Files::copyFile(stored, copy.path())
        || !renameAtAnyLength(copy.path(), destination))
        throw std::runtime_error("cannot copy " + stored.str() + " to "
                                 + destination.str());
}

// The path as one spelling, so a relative path from the compile command and an
// absolute one from $<TARGET_OBJECTS:...> compare equal.
std::string getComparablePath(const std::string& path)
{
    auto absolute = std::filesystem::absolute(std::filesystem::path {path});
    auto error = std::error_code {};
    auto resolved = std::filesystem::weakly_canonical(absolute, error);

    if (error)
        resolved = absolute.lexically_normal();

    auto text = resolved.generic_string();

    if (arePathsCaseInsensitive)
        return toUpperCase(text);

    return text;
}

std::optional<std::size_t> findObjectIndex(const std::vector<std::string>& objects,
                                           const std::string& output)
{
    auto comparableOutput = getComparablePath(output);

    for (auto index = std::size_t {0}; index < objects.size(); ++index)
    {
        if (getComparablePath(objects[index]) == comparableOutput)
            return index;
    }

    return std::nullopt;
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

// What a hit, a store and a miss nobody stored mean for one kind of entry.
struct CountMeanings
{
    std::string_view hit;
    std::string_view miss;
    std::string_view stored;
    std::string_view notStored;
};

const CountMeanings& findCountMeanings(CachedTargetKind kind)
{
    static const auto staticLibrary = CountMeanings {
        "a configure found the library stored, so it was not built",
        "a configure did not find the library stored:",
        "built, then stored by the first link that used it",
        "no link used it: the project does not build it, or has not built yet"};
    static const auto executable = CountMeanings {
        "a configure found the executable stored, so it was copied, not built",
        "a configure did not find the executable stored:",
        "built, then stored by its own link",
        "not linked: the project does not build it, or has not built yet"};
    static const auto objectLibrary = CountMeanings {
        "a configure found the object files stored, so they were copied, not "
        "compiled",
        "a configure did not find the object files stored:",
        "compiled, then stored by the first link that used them",
        "no link used them: the project does not build them, or has not built yet"};

    if (kind == CachedTargetKind::executable)
        return executable;

    if (kind == CachedTargetKind::objectLibrary)
        return objectLibrary;

    return staticLibrary;
}

// Indented under the kind's own line, except for static libraries, whose
// lines stand as they did before the cache stored other kinds.
std::vector<StatisticsLine> createCountLines(CachedTargetKind kind,
                                             const LibraryCacheCounts& counts,
                                             bool hasLocation,
                                             const std::string& indent)
{
    const auto& meanings = findCountMeanings(kind);
    auto missesNotStored = std::max(0, counts.misses - counts.stores);
    auto lines = std::vector<StatisticsLine> {
        {indent + "hits", std::to_string(counts.hits), std::string {meanings.hit}}};

    if (hasLocation || counts.fetchedHits > 0)
        lines.push_back({indent + "  fetched",
                         std::to_string(counts.fetchedHits),
                         "copied from the library cache location, which had it"});

    if (counts.otherToolchainHits > 0)
        lines.push_back({indent + "  another toolchain",
                         std::to_string(counts.otherToolchainHits),
                         "built by another compiler, SDK, CMake or system version, "
                         "which a range admitted"});

    lines.insert(lines.end(),
                 {{indent + "misses",
                   std::to_string(counts.misses),
                   std::string {meanings.miss}},
                  {indent + "  stored",
                   std::to_string(counts.stores),
                   std::string {meanings.stored}},
                  {indent + "  not stored",
                   std::to_string(missesNotStored),
                   std::string {meanings.notStored}}});
    return lines;
}

int countStored(const LibraryStoreSummary& summary, CachedTargetKind kind)
{
    auto found = summary.entryCounts.find(kind);

    if (found == summary.entryCounts.end())
        return 0;

    return found->second;
}

bool hasAnythingToShow(const LibraryStoreSummary& summary, CachedTargetKind kind)
{
    const auto& counts = summary.statistics.getCounts(kind);
    return countStored(summary, kind) > 0 || counts.hits > 0 || counts.misses > 0;
}

// The kinds other than static libraries that have anything stored or counted.
std::vector<CachedTargetKind>
    findOtherKindsToShow(const LibraryStoreSummary& summary)
{
    auto kinds = std::vector<CachedTargetKind> {};

    for (auto kind: getEveryCachedTargetKind())
    {
        if (kind != CachedTargetKind::staticLibrary
            && hasAnythingToShow(summary, kind))
            kinds.push_back(kind);
    }

    return kinds;
}

std::vector<StatisticsLine> createStatisticsLines(const LibraryStoreSummary& summary,
                                                  bool hasLocation)
{
    constexpr auto staticLibrary = CachedTargetKind::staticLibrary;
    auto otherKinds = findOtherKindsToShow(summary);
    auto staticLabel = otherKinds.empty() ? "libraries" : "static libraries";
    auto lines = std::vector<StatisticsLine> {
        {staticLabel,
         std::to_string(countStored(summary, staticLibrary)),
         "stored now"},
        {"size", formatMebibytes(summary.byteCount), "on disk"}};
    auto addLines = [&](const std::vector<StatisticsLine>& added)
    { lines.insert(lines.end(), added.begin(), added.end()); };

    addLines(createCountLines(staticLibrary,
                              summary.statistics.getCounts(staticLibrary),
                              hasLocation,
                              {}));

    for (auto kind: otherKinds)
    {
        lines.push_back({getCachedTargetKindPluralName(kind),
                         std::to_string(countStored(summary, kind)),
                         "stored now"});
        addLines(createCountLines(
            kind, summary.statistics.getCounts(kind), hasLocation, "  "));
    }

    return lines;
}

std::size_t findLabelWidth(const std::vector<StatisticsLine>& lines)
{
    auto width = std::size_t {statisticsLabelWidth};

    for (const auto& line: lines)
        width = std::max(width, line.label.size() + statisticsLabelGap);

    return width;
}

void printLines(const std::vector<StatisticsLine>& lines)
{
    auto labelWidth = findLabelWidth(lines);
    auto valueWidth = std::size_t {statisticsValueWidth};

    for (const auto& line: lines)
        valueWidth = std::max(valueWidth, line.value.size() + statisticsLabelGap);

    for (const auto& line: lines)
    {
        eacp::Console::print(eacp::Strings::format("  {:<{}}{:<{}}{}",
                                                   line.label,
                                                   labelWidth,
                                                   line.value,
                                                   valueWidth,
                                                   line.meaning));
    }
}

std::vector<StatisticsLine>
    createLimitLines(const LibraryCacheStatistics& statistics,
                     const LibraryCacheSizeLimits& limits,
                     bool hasLocation)
{
    auto lines = std::vector<StatisticsLine> {
        {"limit",
         describeByteSize(limits.localBytes),
         "past it, the entries unused for longest are deleted"},
        {"evictions",
         std::to_string(statistics.evictions),
         "entries deleted to stay under the limit"}};

    if (hasLocation)
        lines.insert(lines.end(),
                     {{"location limit",
                       describeByteSize(limits.locationBytes),
                       "the same, at the location"},
                      {"location evictions",
                       std::to_string(statistics.locationEvictions),
                       "entries this machine's sends deleted there"}});

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

    auto summary = LibraryStore {options.cache}.summarize();
    auto lines = createStatisticsLines(summary, hasLocation);
    auto limitLines =
        createLimitLines(summary.statistics, getSizeLimits(options), hasLocation);
    lines.insert(lines.end(), limitLines.begin(), limitLines.end());
    printLines(lines);
}

struct SettingsInForce
{
    LibraryCacheSettings settings;
    std::optional<eacp::FilePath> projectFile;
};

// What a cache command run here reads: the environment and the lockstep.json
// beside the lock of the project around the working directory. A build
// directory's -D is not read, since these commands run outside any build.
SettingsInForce findSettingsInForce(const CommandOptions& options)
{
    auto inForce = SettingsInForce {};
    auto lockFile = findEnclosingLockFile(std::filesystem::current_path());

    if (lockFile && eacp::Files::isFile(getProjectSettingsPath(*lockFile)))
        inForce.projectFile = getProjectSettingsPath(*lockFile);

    inForce.settings = resolveLibraryCacheSettings(
        createSettingSources({},
                             options.settingEnvironment,
                             inForce.projectFile.value_or(eacp::FilePath {})));
    return inForce;
}

std::string describeSource(const SettingsInForce& inForce, SettingSource source)
{
    if (source == SettingSource::projectFile && inForce.projectFile)
        return inForce.projectFile->str();

    return describeSettingSource(source);
}

void printSettings(const SettingsInForce& inForce)
{
    auto lines = std::vector<StatisticsLine> {};

    for (const auto& description: getSettingDescriptions())
    {
        const auto& value = inForce.settings.*description.member;
        auto text = value.text.empty() ? std::string {"(unset)"} : value.text;
        lines.push_back({std::string {description.variable},
                         text,
                         describeSource(inForce, value.source)});
    }

    eacp::Console::print("settings here, and where each comes from (a -D on a build "
                         "directory wins over them there)");
    printLines(lines);
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

int runLibraryCacheCommand(const CommandOptions& commandOptions)
{
    auto settings = resolveLibraryCacheSettings(
        createSettingSources(commandOptions.cacheVariables,
                             commandOptions.settingEnvironment,
                             getProjectSettingsPath(commandOptions.lock)));
    auto limits = parseSizeLimits(settings);
    auto options = commandOptions;
    options.libraryCacheLocation = settings.location.text;
    options.libraryCacheMaxSize = std::to_string(limits.localBytes);
    options.libraryCacheLocationMaxSize = std::to_string(limits.locationBytes);
    auto description =
        parseConfigureDescription(eacp::Files::readFile(options.input));
    auto packages = findConfiguredPackages(options);
    auto variableNames = collectVariableNamesOfCachedPackages(packages);
    auto inputs = LibraryCacheInputs {
        description,
        packages,
        variableNames,
        {toForwardSlashes(options.buildDirectory.str()),
         toForwardSlashes(options.cache.str())},
        hashCompilerExecutables(description, options.cache / "lib")};
    auto store = LibraryStore {options.cache};
    auto planning =
        LibraryCachePlanning {inputs, store, parseToolchainRanges(settings)};
    auto plan = planLibraryCache(planning);
    auto locationReport = fetchFromLibraryCacheLocation(options, plan, planning);
    markHitsUsed(store, plan);
    auto evictionReport = std::string {};

    if (auto evicted = evictFromStore(store, limits.localBytes); !evicted.empty())
        evictionReport = createMessage("STATUS", evicted);
    addToStatisticsOrWarn(store, countHitsAndMisses(plan));
    auto snippet = createPlanSnippet(options, description, plan);
    writeLibraryCacheFiles(options, plan);
    eacp::Files::createDirectories(options.output.parentDirectory());
    eacp::Files::writeFile(options.output,
                           std::string_view {snippet + locationReport
                                             + createReport(plan) + evictionReport});
    return 0;
}

int runUseCachedLibraryCommand(const CommandOptions& options)
{
    if (options.libraries.size() != 1)
        throw std::runtime_error("use-cached-library takes one --library");

    auto stored = eacp::FilePath {options.libraries[0]};
    copyIntoPlace(stored, options.output);
    markStoredFileUsed(options, stored);
    return 0;
}

int runUseCachedObjectsCommand(const CommandOptions& options)
{
    const auto& command = options.argumentsAfterSeparator;
    auto objects = splitCMakeList(
        eacp::Strings::trim(eacp::Files::readFile(options.objectList)));
    auto storedObjects = readStoredObjectList(options.storedObjectList);
    auto index = findObjectIndex(objects, options.object);
    auto isStoredInSameOrder = objects.size() == storedObjects.size();

    // A compile that writes none of the stored objects, such as a precompiled
    // header's, runs as it is.
    if (!index || !isStoredInSameOrder)
    {
        printStatus(options.object
                    + " is not one of the object files the library cache stored "
                      "for its target, so it compiles");
        return runWrappedCommand(command, "use-cached-objects");
    }

    auto stored = eacp::FilePath {storedObjects[*index]};
    copyIntoPlace(stored, eacp::FilePath {options.object});
    markStoredFileUsed(options, stored);
    return 0;
}

int runStoreLibrariesCommand(const CommandOptions& options)
{
    const auto& command = options.argumentsAfterSeparator;

    if (auto exitCode = runWrappedCommand(command, "store-libraries"); exitCode != 0)
        return exitCode;

    storeLinkedEntries(options, command);
    return 0;
}

std::vector<EntryAddress>
    parseAddressesToSend(const eacp::Vector<std::string>& texts)
{
    auto addresses = std::vector<EntryAddress> {};

    for (const auto& text: texts)
    {
        auto address = parseEntryAddress(text);

        if (!address)
            throw std::runtime_error(
                "send-libraries takes --library=<key>/<build id>, not " + text);

        addresses.push_back(*address);
    }

    return addresses;
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
    auto keys = parseAddressesToSend(options.libraries);

    try
    {
        auto local = LibraryStore {options.cache};
        auto evicted =
            LibraryCacheAtLocation {*location, options.cmake}.sendLibraries(
                keys, local, {getSizeLimits(options).locationBytes});
        printStatus("sent " + std::to_string(keys.size()) + " libraries to "
                    + location->text + "; evicted " + std::to_string(evicted)
                    + " there to stay under its size limit");
        countLocationEvictions(local, evicted);
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

int runCacheCommand(const CommandOptions& commandOptions)
{
    auto inForce = findSettingsInForce(commandOptions);
    auto options = commandOptions;
    auto limits = parseSizeLimits(inForce.settings);
    options.libraryCacheLocation = inForce.settings.location.text;
    options.libraryCacheMaxSize = std::to_string(limits.localBytes);
    options.libraryCacheLocationMaxSize = std::to_string(limits.locationBytes);
    auto subcommand = options.names.empty() ? std::string {} : options.names[0];

    if (subcommand == "stats" && options.names.size() == 1)
    {
        printStoreSummary(options);
        printSettings(inForce);
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
