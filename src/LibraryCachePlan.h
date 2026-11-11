#pragma once

#include "Commands.h"
#include "ConfigureDescription.h"
#include "KeyText.h"
#include "LibraryStore.h"

#include <eacp/Core/Utils/FilePath.h>

#include <optional>
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

// The stored entry for `key`, when it is there whole and holds `kind`.
std::optional<StoredEntry> findEntryOfKind(const LibraryStore& store,
                                           const std::string& key,
                                           CachedTargetKind kind);

// Keys every static and object library of a locked package first, then every
// executable, whose key covers the keys of the libraries it links.
LibraryCachePlan planLibraryCache(const LibraryCacheInputs& inputs,
                                  const LibraryStore& store);

eacp::FilePath getLibraryCacheDirectory(const eacp::FilePath& buildDirectory);

eacp::FilePath getInputsPath(const eacp::FilePath& buildDirectory,
                             const std::string& key);

// The lockstep the CMake module runs, from LOCKSTEP_EXECUTABLE.
std::string getLockstepExecutable();

// The options every library cache command the build runs takes.
eacp::Vector<std::string> createSharedArguments(const CommandOptions& options);

// The command, as the build runs it, that starts `lockstep <command>` with the
// options every library cache command the build runs takes.
std::string createLauncher(const CommandOptions& options, std::string_view command);

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
