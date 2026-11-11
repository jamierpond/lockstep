#pragma once

#include <compare>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lockstep
{
using RecordFields = std::vector<std::pair<std::string, std::string>>;

// What lockstep-library-cache.cmake writes at the end of a configure, one
// "<kind>\t<subject>\t<field>\t<value>" line per field, grouped by subject.
struct ConfigureDescription
{
    RecordFields globals;
    std::map<std::string, RecordFields> targets;
    std::map<std::string, RecordFields> directories;
    std::map<std::string, RecordFields> variables;
};

std::string toForwardSlashes(std::string path);

ConfigureDescription parseConfigureDescription(std::string_view text);

// The field's value, or empty when the subject has none.
std::string findField(const RecordFields& fields, std::string_view name);

std::string findTargetField(const ConfigureDescription& description,
                            const std::string& target,
                            std::string_view field);

const RecordFields& findSubject(const std::map<std::string, RecordFields>& subjects,
                                const std::string& subject);

// The items of a CMake list, empty ones left out.
std::vector<std::string> splitCMakeList(const std::string& list);

std::string replaceAll(std::string text, std::string_view from, std::string_view to);

std::string toUpperCase(std::string text);

bool isInsideDirectory(const std::string& path, const std::string& directory);

// A package added this configure. `hash` is its cache entry src/<hash>, and is
// empty for one from a local SOURCE_DIR, a <Name>_SOURCE override or a
// SOURCE_DIR checkout, whose files no hash covers.
struct ConfiguredPackage
{
    std::string name;
    std::string hash;
    std::string sourceDirectory;
};

// The upper-case variable names each cached package's CMake files mention, by
// package hash: the variables that package's CMake could have read.
using PackageVariableNames = std::map<std::string, std::set<std::string>>;

// The paths that differ between build directories and machines, which the key
// text replaces with fixed words so two build directories share entries. The
// key still covers the cache directory itself (see computeLibraryKey).
struct MachinePaths
{
    std::string buildDirectory;
    std::string cacheDirectory;
};

// A compiler as a configure names it: CMAKE_<LANG>_COMPILER and
// CMAKE_<LANG>_COMPILER_ID.
struct ConfiguredCompiler
{
    std::string language;
    std::string compilerId;
    std::string path;

    auto operator<=>(const ConfiguredCompiler&) const = default;
};

// The SHA-256 of each compiler's executable files; see CompilerExecutable.h.
using CompilerExecutableHashes = std::map<ConfiguredCompiler, std::string>;

struct LibraryCacheInputs
{
    const ConfigureDescription& description;
    const std::vector<ConfiguredPackage>& packages;
    const PackageVariableNames& variableNames;
    MachinePaths paths;
    CompilerExecutableHashes compilerExecutableHashes {};
};

std::optional<ConfiguredPackage>
    findOwningPackage(const std::vector<ConfiguredPackage>& packages,
                      const std::string& directory);

// Every target `target` links, and what those pass on through their
// INTERFACE_LINK_LIBRARIES, by name.
std::set<std::string> collectLinkClosure(const ConfigureDescription& description,
                                         const std::string& target);
} // namespace lockstep
