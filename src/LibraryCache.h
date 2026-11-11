#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lockstep
{
// Raised whenever what goes into a key or the store's files change meaning, so
// no older entry hits and the store reads no older file.
constexpr auto libraryCacheFormatVersion = 5;

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

struct LibraryCacheInputs
{
    const ConfigureDescription& description;
    const std::vector<ConfiguredPackage>& packages;
    const PackageVariableNames& variableNames;
    MachinePaths paths;
};

struct LibraryKey
{
    std::string key;

    // What the key is the SHA-256 of.
    std::string inputs;
};

std::optional<ConfiguredPackage>
    findOwningPackage(const std::vector<ConfiguredPackage>& packages,
                      const std::string& directory);

// Every target `target` links, and what those pass on through their
// INTERFACE_LINK_LIBRARIES, by name.
std::set<std::string> collectLinkClosure(const ConfigureDescription& description,
                                         const std::string& target);

std::optional<std::string> findReasonNotToCache(const LibraryCacheInputs& inputs,
                                                const std::string& target);

// The package sources live in the cache directory, and object code can hold
// their absolute paths (__FILE__, debug information), so the key covers the
// cache directory unless the target's flags map it away with
// -ffile-prefix-map, or with both -fmacro-prefix-map and -fdebug-prefix-map.
LibraryKey computeLibraryKey(const LibraryCacheInputs& inputs,
                             const std::string& target);

std::set<std::string> collectUpperCaseIdentifiers(std::string_view text);

std::set<std::string>
    collectPackageVariableNames(const eacp::FilePath& sourceDirectory);

// True when `library` is one of the command's arguments, or is named in a
// response file (@file) among them, so the command really reads it.
bool isCommandInput(const std::vector<std::string>& command,
                    const eacp::FilePath& library,
                    const eacp::FilePath& workingDirectory);
} // namespace lockstep
