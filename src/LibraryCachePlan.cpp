#include "LibraryCachePlan.h"
#include "CMakeSnippet.h"
#include "ExecutableCache.h"
#include "LibraryCache.h"
#include "PackageArguments.h"

#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

#include <Miro/Json.h>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <stdexcept>

namespace lockstep
{
namespace
{
constexpr auto stubDeclaration =
    std::string_view {"typedef int lockstep_library_cache_stub;\n"};
constexpr auto stubFileNames =
    std::array<std::pair<std::string_view, std::string_view>, 2> {
        {{"CXX", "stub.cpp"}, {"C", "stub.c"}}};
constexpr auto linkingTargetTypes = std::array<std::string_view, 3> {
    "EXECUTABLE", "SHARED_LIBRARY", "MODULE_LIBRARY"};

// The hits, misses and skips of one kind of entry in a configure.
struct KindCounts
{
    int hits = 0;
    int otherToolchainHits = 0;
    int misses = 0;
    int skipped = 0;
};

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

// The entry holds `kind`: one file, unless it is an object library's objects.
bool holdsKind(const StoredEntry& entry, CachedTargetKind kind)
{
    if (entry.manifest.kind != kind)
        return false;

    auto isOneFile = entry.manifest.files.size() == 1;
    return kind == CachedTargetKind::objectLibrary || isOneFile;
}

bool isNewerThanChosen(const BuildChoice& choice, const StoredEntry& stored)
{
    if (!choice.entry)
        return true;

    return compareNewness(stored.manifest.builtWith,
                          choice.entry->manifest.builtWith)
           == std::strong_ordering::greater;
}

// Takes `stored` when the ranges admit it and it is newer than the build taken
// so far, else records why not.
void considerBuild(BuildChoice& choice,
                   const StoredEntry& stored,
                   const BuiltWith& thisMachine,
                   const ToolchainRanges& ranges)
{
    auto reason =
        findReasonToRefuseBuild(stored.manifest.builtWith, thisMachine, ranges);

    if (reason)
        choice.refusals.push_back(*reason);
    else if (isNewerThanChosen(choice, stored))
        choice.entry = stored;
}

bool isPackageTarget(const LibraryCacheInputs& inputs, const RecordFields& fields)
{
    return !isCMakeTrue(findField(fields, "IMPORTED"))
           && findOwningPackage(inputs.packages, findField(fields, "SOURCE_DIR"));
}

// The kind of a static or object library a package declares.
std::optional<CachedTargetKind>
    findPackageLibraryKind(const LibraryCacheInputs& inputs,
                           const RecordFields& fields)
{
    auto kind = findCachedTargetKind(fields);

    if (kind == CachedTargetKind::executable || !isPackageTarget(inputs, fields))
        return std::nullopt;

    return kind;
}

LibraryDecision decide(const LibraryCachePlanning& planning,
                       const std::string& target,
                       CachedTargetKind kind,
                       const LibraryKey& key)
{
    auto choice = chooseStoredBuild(planning.store, key, kind, planning.ranges);
    return {target, kind, key, choice.entry, false, choice.refusals};
}

void planLibraries(LibraryCachePlan& plan,
                   LinkedLibraryKeys& libraryKeys,
                   const LibraryCachePlanning& planning)
{
    const auto& inputs = planning.inputs;

    for (const auto& [target, fields]: inputs.description.targets)
    {
        auto kind = findPackageLibraryKind(inputs, fields);

        if (!kind)
            continue;

        if (auto reason = findReasonNotToCache(inputs, target))
        {
            plan.skipped.push_back({target, *kind, *reason});
            continue;
        }

        auto key = computeLibraryKey(inputs, target);
        auto decision = decide(planning, target, *kind, key);
        auto linkedBuildId =
            decision.cached ? decision.cached->address.buildId : key.buildId;
        libraryKeys[target] = {key.key, linkedBuildId};
        plan.decisions.push_back(decision);
    }
}

void planExecutables(LibraryCachePlan& plan,
                     const LinkedLibraryKeys& libraryKeys,
                     const LibraryCachePlanning& planning)
{
    constexpr auto kind = CachedTargetKind::executable;
    const auto& inputs = planning.inputs;

    for (const auto& [target, fields]: inputs.description.targets)
    {
        if (findCachedTargetKind(fields) != kind || !isPackageTarget(inputs, fields))
            continue;

        if (auto reason =
                findReasonNotToCacheExecutable(inputs, target, libraryKeys))
        {
            plan.skipped.push_back({target, kind, *reason});
            continue;
        }

        auto key = computeExecutableKey(inputs, target, libraryKeys);
        plan.decisions.push_back(decide(planning, target, kind, key));
    }
}

std::string createPropertySetting(const std::string& target,
                                  std::string_view property,
                                  const std::string& value)
{
    return "set_property(TARGET " + quoteForCMake(target) + " PROPERTY "
           + std::string {property} + " " + value + ")\n";
}

std::string createStubSources(const CommandOptions& options,
                              const RecordFields& fields)
{
    auto kept = findField(fields, "KEPT_SOURCES");
    auto stub =
        getStubPath(options.buildDirectory, findField(fields, "STUB_LANGUAGE"));
    auto sources = quoteForCMake(stub.str());

    if (!kept.empty())
        sources = quoteForCMake(kept) + " " + sources;

    return sources;
}

std::string createCopyingLinkLauncher(const CommandOptions& options,
                                      const LibraryDecision& decision)
{
    auto stored = decision.cached->getFilePaths().front();
    return createLauncher(options, "use-cached-library") + " "
           + quoteForShell("--library=" + stored.str()) + " "
           + quoteForShell("--output=$<TARGET_FILE:" + decision.target + ">")
           + " --";
}

// One stored object path per line, in $<TARGET_OBJECTS:...> order.
std::string listStoredObjects(const StoredEntry& entry)
{
    auto lines = std::string {};

    for (const auto& object: entry.getFilePaths())
        lines += object.str() + "\n";

    return lines;
}

// The target compiles one empty stub instead of its sources, and its archive
// step copies the cached library into place instead of archiving the stub.
std::string createStaticLibraryHitSnippet(const CommandOptions& options,
                                          const RecordFields& fields,
                                          const LibraryDecision& decision)
{
    return createPropertySetting(
               decision.target, "SOURCES", createStubSources(options, fields))
           + createPropertySetting(
               decision.target,
               "RULE_LAUNCH_LINK",
               quoteForCMake(createCopyingLinkLauncher(options, decision)));
}

// As for a static library, and the executable links nothing, so no library it
// would have linked is built for it.
std::string createExecutableHitSnippet(const CommandOptions& options,
                                       const RecordFields& fields,
                                       const LibraryDecision& decision)
{
    return createStaticLibraryHitSnippet(options, fields, decision)
           + createPropertySetting(decision.target, "LINK_LIBRARIES", "");
}

// The target keeps its sources, so $<TARGET_OBJECTS:...> names the same files,
// and each compile copies its stored object into place instead of compiling.
// The generator replaces <OBJECT> with the object file each compile writes.
// Both lists of objects go into files, not onto the compile line: NMake fails
// to read some long compile lines ("')' missing in macro invocation").
std::string createObjectLibraryHitSnippet(const CommandOptions& options,
                                          const LibraryDecision& decision)
{
    const auto& key = decision.key.key;
    auto storedList = getStoredObjectListPath(options.buildDirectory, key);
    auto targetList = getTargetObjectListPath(options.buildDirectory, key);
    auto launcher = createLauncher(options, "use-cached-objects") + " "
                    + quoteForShell("--stored-object-list=" + storedList.str()) + " "
                    + quoteForShell("--object-list=" + targetList.str()) + " "
                    + quoteForShell("--object=<OBJECT>") + " --";
    return "file(GENERATE OUTPUT " + quoteForCMake(targetList.str()) + " CONTENT "
           + quoteForCMake("$<TARGET_OBJECTS:" + decision.target + ">") + ")\n"
           + createPropertySetting(
               decision.target, "RULE_LAUNCH_COMPILE", quoteForCMake(launcher));
}

std::string createHitSnippet(const CommandOptions& options,
                             const ConfigureDescription& description,
                             const LibraryDecision& decision)
{
    const auto& fields = description.targets.at(decision.target);

    switch (decision.kind)
    {
        case CachedTargetKind::staticLibrary:
            return createStaticLibraryHitSnippet(options, fields, decision);
        case CachedTargetKind::executable:
            return createExecutableHitSnippet(options, fields, decision);
        case CachedTargetKind::objectLibrary:
            return createObjectLibraryHitSnippet(options, decision);
    }

    throw std::logic_error("the library cache has no hit for this kind of entry");
}

bool isLinkingTarget(const RecordFields& fields)
{
    auto type = findField(fields, "TYPE");
    return std::ranges::find(linkingTargetTypes, type) != linkingTargetTypes.end()
           && !isCMakeTrue(findField(fields, "IMPORTED"));
}

bool takesObjectsOf(const RecordFields& fields, const std::string& objectLibrary)
{
    return findField(fields, "SOURCES")
               .find("$<TARGET_OBJECTS:" + objectLibrary + ">")
           != std::string::npos;
}

// How store-libraries is told to store each kind: the option, and the
// generator expression for the files.
struct StoringOption
{
    CachedTargetKind kind;
    std::string_view option;
    std::string_view filesExpression;
};

constexpr auto storingOptions = std::array<StoringOption, 3> {
    {{CachedTargetKind::staticLibrary, "--library", "TARGET_FILE"},
     {CachedTargetKind::executable, "--executable", "TARGET_FILE"},
     {CachedTargetKind::objectLibrary, "--object-library", "TARGET_OBJECTS"}}};

std::string createStoringOption(const LibraryDecision& missed)
{
    for (const auto& storing: storingOptions)
    {
        if (storing.kind == missed.kind)
            return " "
                   + quoteForShell(std::string {storing.option} + "="
                                   + missed.key.key + "=$<"
                                   + std::string {storing.filesExpression} + ":"
                                   + missed.target + ">");
    }

    throw std::logic_error("the library cache cannot store this kind of entry");
}

// A missed executable is stored by its own link; a missed library or object
// library by the link of every target that links it, or takes its objects.
bool isStoredByLinkOf(const LibraryDecision& missed,
                      const std::string& linking,
                      const RecordFields& linkingFields,
                      const std::set<std::string>& linkClosure)
{
    if (missed.kind == CachedTargetKind::executable)
        return missed.target == linking;

    if (linkClosure.contains(missed.target))
        return true;

    return missed.kind == CachedTargetKind::objectLibrary
           && takesObjectsOf(linkingFields, missed.target);
}

// The store-libraries options that store the misses this target's link uses.
std::string createStoringOptions(const ConfigureDescription& description,
                                 const std::string& target,
                                 const LibraryCachePlan& plan)
{
    const auto& fields = description.targets.at(target);
    auto closure = collectLinkClosure(description, target);
    auto options = std::string {};

    for (const auto& decision: plan.decisions)
    {
        if (!decision.cached && isStoredByLinkOf(decision, target, fields, closure))
            options += createStoringOption(decision);
    }

    return options;
}

bool isHit(const LibraryCachePlan& plan, const std::string& target)
{
    auto isHitForTarget = [&](const LibraryDecision& decision)
    { return decision.cached && decision.target == target; };

    return std::ranges::any_of(plan.decisions, isHitForTarget);
}

// Every executable or shared library that links a missed library or object
// library stores it once its own link succeeds, so only what a build really
// linked is stored; a missed executable stores itself the same way.
std::string createStoringSnippet(const CommandOptions& options,
                                 const ConfigureDescription& description,
                                 const LibraryCachePlan& plan)
{
    auto snippet = std::string {};

    for (const auto& [target, fields]: description.targets)
    {
        if (!isLinkingTarget(fields) || isHit(plan, target))
            continue;

        auto storing = createStoringOptions(description, target, plan);

        if (storing.empty())
            continue;

        auto launcher = createLauncher(options, "store-libraries");

        for (const auto& argument: createSizeLimitArguments(options))
            launcher += " " + quoteForShell(argument);

        launcher += storing + " -- " + findField(fields, "EXISTING_LINK_LAUNCHER");
        snippet += createPropertySetting(
            target, "RULE_LAUNCH_LINK", quoteForCMake(launcher));
    }

    return snippet;
}

// A static library by its name alone, as before the cache stored other kinds.
std::string describeTarget(CachedTargetKind kind, const std::string& target)
{
    if (kind == CachedTargetKind::staticLibrary)
        return target;

    return getCachedTargetKindName(kind) + " " + target;
}

std::string describeWhoStores(CachedTargetKind kind)
{
    if (kind == CachedTargetKind::executable)
        return "its own link stores it";

    return "the first link that uses it stores it";
}

std::map<CachedTargetKind, KindCounts> countEachKind(const LibraryCachePlan& plan)
{
    auto counts = std::map<CachedTargetKind, KindCounts> {
        {CachedTargetKind::staticLibrary, {}}};

    for (const auto& decision: plan.decisions)
    {
        auto& kindCounts = counts[decision.kind];

        if (decision.cached)
            ++kindCounts.hits;
        else
            ++kindCounts.misses;

        if (decision.isBuiltByAnotherToolchain())
            ++kindCounts.otherToolchainHits;
    }

    for (const auto& skipped: plan.skipped)
        ++counts[skipped.kind].skipped;

    return counts;
}

std::string describeHits(const KindCounts& counts)
{
    auto hits = std::to_string(counts.hits) + " hits";

    if (counts.otherToolchainHits == 0)
        return hits;

    return hits + " (" + std::to_string(counts.otherToolchainHits)
           + " built by another toolchain)";
}

std::string describeCounts(const KindCounts& counts)
{
    return describeHits(counts) + ", " + std::to_string(counts.misses) + " misses, "
           + std::to_string(counts.skipped) + " skipped";
}

std::string describeHit(const LibraryDecision& decision)
{
    auto details = decision.key.key;

    if (decision.isFetched)
        details += ", fetched";

    if (decision.isBuiltByAnotherToolchain())
        details += "; built with "
                   + describeDifferences(decision.cached->manifest.builtWith,
                                         decision.key.builtWith);

    return "library cache hit for " + describeTarget(decision.kind, decision.target)
           + " (" + details + ")";
}

std::string describeMiss(const LibraryDecision& decision)
{
    auto line = "library cache miss for "
                + describeTarget(decision.kind, decision.target) + " ("
                + decision.key.key + "); " + describeWhoStores(decision.kind);

    for (const auto& refusal: decision.refusals)
        line += "; a stored build is not taken: " + refusal;

    return line;
}

// "<n> hits, <n> misses, <n> skipped" when only static libraries were
// considered, else the same for each kind under its name.
std::string describeSummary(const LibraryCachePlan& plan)
{
    auto counts = countEachKind(plan);

    if (counts.size() == 1)
        return describeCounts(counts.at(CachedTargetKind::staticLibrary));

    auto parts = eacp::Vector<std::string> {};

    for (auto kind: getEveryCachedTargetKind())
    {
        if (auto found = counts.find(kind); found != counts.end())
            parts.add(getCachedTargetKindPluralName(kind) + ": "
                      + describeCounts(found->second));
    }

    return eacp::Strings::join(parts, "; ");
}

std::string joinWithAnd(const eacp::Vector<std::string>& items)
{
    if (items.size() <= 1)
        return eacp::Strings::join(items, "");

    auto allButLast = eacp::Vector<std::string> {};

    for (auto index = std::size_t {0}; index + 1 < items.size(); ++index)
        allButLast.add(items[index]);

    return eacp::Strings::join(allButLast, ", ") + " and " + items.back();
}
} // namespace

bool LibraryDecision::isBuiltByAnotherToolchain() const
{
    return cached && cached->address.buildId != key.buildId;
}

BuildChoice chooseStoredBuild(const LibraryStore& store,
                              const LibraryKey& key,
                              CachedTargetKind kind,
                              const ToolchainRanges& ranges)
{
    auto own = store.findEntry({key.key, key.buildId});

    if (own && holdsKind(*own, kind))
        return {own, {}};

    auto choice = BuildChoice {};

    for (const auto& stored: store.listBuilds(key.key))
    {
        if (holdsKind(stored, kind) && stored.address.buildId != key.buildId)
            considerBuild(choice, stored, key.builtWith, ranges);
    }

    return choice;
}

LibraryCachePlan planLibraryCache(const LibraryCachePlanning& planning)
{
    auto plan = LibraryCachePlan {};
    auto libraryKeys = LinkedLibraryKeys {};
    planLibraries(plan, libraryKeys, planning);
    planExecutables(plan, libraryKeys, planning);
    return plan;
}

eacp::FilePath getLibraryCacheDirectory(const eacp::FilePath& buildDirectory)
{
    return buildDirectory / "_lockstep" / "library-cache";
}

eacp::FilePath getInputsPath(const eacp::FilePath& buildDirectory,
                             const std::string& key)
{
    return getLibraryCacheDirectory(buildDirectory) / "inputs" / (key + ".txt");
}

eacp::FilePath getBuiltWithPath(const eacp::FilePath& buildDirectory,
                                const std::string& key)
{
    return getLibraryCacheDirectory(buildDirectory) / "inputs"
           / (key + ".built-with.json");
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

eacp::Vector<std::string> createSizeLimitArguments(const CommandOptions& options)
{
    auto arguments = eacp::Vector<std::string> {};

    if (!options.libraryCacheMaxSize.empty())
        arguments.add("--library-cache-max-size=" + options.libraryCacheMaxSize);

    if (!options.libraryCacheLocationMaxSize.empty())
        arguments.add("--library-cache-location-max-size="
                      + options.libraryCacheLocationMaxSize);

    return arguments;
}

eacp::FilePath getStoredObjectListPath(const eacp::FilePath& buildDirectory,
                                       const std::string& key)
{
    return getLibraryCacheDirectory(buildDirectory) / "objects" / (key + ".txt");
}

eacp::FilePath getTargetObjectListPath(const eacp::FilePath& buildDirectory,
                                       const std::string& key)
{
    return getLibraryCacheDirectory(buildDirectory) / "objects"
           / (key + ".target.txt");
}

std::string quoteForShell(const std::string& text)
{
    return "\"" + text + "\"";
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

std::string createPlanSnippet(const CommandOptions& options,
                              const ConfigureDescription& description,
                              const LibraryCachePlan& plan)
{
    auto snippet = std::string {};

    for (const auto& decision: plan.decisions)
    {
        if (decision.cached)
            snippet += createHitSnippet(options, description, decision);
    }

    return snippet + createStoringSnippet(options, description, plan);
}

std::string createMessage(std::string_view level, const std::string& text)
{
    return "message(" + std::string {level} + " "
           + quoteForCMake("lockstep: " + text) + ")\n";
}

std::string createReport(const LibraryCachePlan& plan)
{
    auto report = std::string {};

    for (const auto& decision: plan.decisions)
    {
        if (decision.cached)
            report += createMessage("VERBOSE", describeHit(decision));
        else
            report += createMessage("VERBOSE", describeMiss(decision));
    }

    for (const auto& skipped: plan.skipped)
        report += createMessage("VERBOSE",
                                "library cache skips "
                                    + describeTarget(skipped.kind, skipped.target)
                                    + ": " + skipped.reason);

    return report
           + createMessage("STATUS",
                           "library cache: " + describeSummary(plan)
                               + " (--log-level=VERBOSE names them)");
}

std::string describeEntriesInPlural(const LibraryCachePlan& plan)
{
    auto counts = countEachKind(plan);

    if (counts.size() == 1)
        return "libraries";

    auto names = eacp::Vector<std::string> {};

    for (auto kind: getEveryCachedTargetKind())
    {
        if (counts.contains(kind))
            names.add(getCachedTargetKindPluralName(kind));
    }

    return joinWithAnd(names);
}

LibraryCacheStatistics countHitsAndMisses(const LibraryCachePlan& plan)
{
    auto statistics = LibraryCacheStatistics {};

    for (const auto& decision: plan.decisions)
    {
        auto& counts = statistics.getCounts(decision.kind);

        if (decision.cached)
            ++counts.hits;
        else
            ++counts.misses;

        if (decision.isFetched)
            ++counts.fetchedHits;

        if (decision.isBuiltByAnotherToolchain())
            ++counts.otherToolchainHits;
    }

    return statistics;
}

void writeLibraryCacheFiles(const CommandOptions& options,
                            const LibraryCachePlan& plan)
{
    for (const auto& [language, fileName]: stubFileNames)
        writeFileIfChanged(getLibraryCacheDirectory(options.buildDirectory)
                               / fileName,
                           stubDeclaration);

    for (const auto& decision: plan.decisions)
    {
        if (decision.cached && decision.kind == CachedTargetKind::objectLibrary)
            writeFileIfChanged(
                getStoredObjectListPath(options.buildDirectory, decision.key.key),
                listStoredObjects(*decision.cached));

        writeFileIfChanged(getInputsPath(options.buildDirectory, decision.key.key),
                           decision.key.inputs);
        writeFileIfChanged(
            getBuiltWithPath(options.buildDirectory, decision.key.key),
            Miro::toJSONString(decision.key.builtWith));
    }
}
} // namespace lockstep
