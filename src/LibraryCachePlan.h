#pragma once

#include "Commands.h"
#include "ConfigureDescription.h"
#include "KeyText.h"
#include "LibraryStore.h"

#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace lockstep
{
// What the configure decided for one static library, executable or object
// library a locked package builds.
struct LibraryDecision
{
    std::string target;
    CachedTargetKind kind = CachedTargetKind::staticLibrary;
    LibraryKey key;
    std::optional<StoredEntry> cached;
    bool isFetched = false;

    // Why each stored build of the key that is not taken was refused.
    std::vector<std::string> refusals;

    // A hit on a build another toolchain made, which a range admitted.
    bool isBuiltByAnotherToolchain() const;
};

struct SkippedLibrary
{
    std::string target;
    CachedTargetKind kind = CachedTargetKind::staticLibrary;
    std::string reason;
};

struct LibraryCachePlan
{
    std::vector<LibraryDecision> decisions;
    std::vector<SkippedLibrary> skipped;
};

// Which stored build of a key a lookup takes, and why it refused the others.
struct BuildChoice
{
    std::optional<StoredEntry> entry;
    std::vector<std::string> refusals;
};

// This machine's own build of the key when it is stored, else the newest
// build the ranges admit; only builds that are whole and hold `kind` count.
BuildChoice chooseStoredBuild(const LibraryStore& store,
                              const LibraryKey& key,
                              CachedTargetKind kind,
                              const ToolchainRanges& ranges);

// What a configure plans from: the targets, this machine's store and the
// ranges of other toolchains' builds it takes.
struct LibraryCachePlanning
{
    const LibraryCacheInputs& inputs;
    const LibraryStore& store;
    ToolchainRanges ranges;
};

// The project's own targets, the packages' executables, everything those link
// and the object libraries whose objects they take. The cache neither looks up
// nor stores a package library outside it, which nothing here links.
std::set<std::string> collectTargetsTheBuildUses(const LibraryCacheInputs& inputs);

// Keys every static and object library of a locked package first, then every
// executable, whose key covers the keys of the libraries it links.
LibraryCachePlan planLibraryCache(const LibraryCachePlanning& planning);

eacp::FilePath getLibraryCacheDirectory(const eacp::FilePath& buildDirectory);

eacp::FilePath getInputsPath(const eacp::FilePath& buildDirectory,
                             const std::string& key);

// Where the configure writes the record of this machine's build of `key`,
// which the link that stores it reads.
eacp::FilePath getBuiltWithPath(const eacp::FilePath& buildDirectory,
                                const std::string& key);

// The lockstep the CMake module runs, from LOCKSTEP_EXECUTABLE.
std::string getLockstepExecutable();

// The options every library cache command the build runs takes.
eacp::Vector<std::string> createSharedArguments(const CommandOptions& options);

// The command, as the build runs it, that starts `lockstep <command>` with the
// options every library cache command the build runs takes.
std::string createLauncher(const CommandOptions& options, std::string_view command);

// The size limits, which only the commands that store or send need.
eacp::Vector<std::string> createSizeLimitArguments(const CommandOptions& options);

// The stored objects use-cached-objects copies for an object library hit, one
// path per line, so the compile line, which NMake has to read, stays short.
eacp::FilePath getStoredObjectListPath(const eacp::FilePath& buildDirectory,
                                       const std::string& key);

// $<TARGET_OBJECTS:...> of an object library hit, which file(GENERATE) writes
// as a ;-list: the object files its compiles write.
eacp::FilePath getTargetObjectListPath(const eacp::FilePath& buildDirectory,
                                       const std::string& key);

std::string quoteForShell(const std::string& text);

// The CMake that makes every hit build from the store and every link store
// what it built.
std::string createPlanSnippet(const CommandOptions& options,
                              const ConfigureDescription& description,
                              const LibraryCachePlan& plan);

std::string createMessage(std::string_view level, const std::string& text);

// The configure's lines: each target at VERBOSE, then the counts.
std::string createReport(const LibraryCachePlan& plan);

// "libraries" when the plan holds static libraries only, else the plural names
// of the kinds it holds.
std::string describeEntriesInPlural(const LibraryCachePlan& plan);

LibraryCacheStatistics countHitsAndMisses(const LibraryCachePlan& plan);

// The stub sources, and the key inputs of every key, in the build directory.
void writeLibraryCacheFiles(const CommandOptions& options,
                            const LibraryCachePlan& plan);
} // namespace lockstep
