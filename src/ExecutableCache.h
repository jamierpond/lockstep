#pragma once

#include "ConfigureDescription.h"
#include "KeyText.h"

#include <map>
#include <optional>
#include <string>

namespace lockstep
{
// A static or object library the cache stores this configure: its key, and the
// build of it this build directory links.
struct LinkedLibrary
{
    std::string key;
    std::string buildId;
};

// Every static and object library the cache stores this configure, by target
// name.
using LinkedLibraryKeys = std::map<std::string, LinkedLibrary>;

// Why an executable a package builds cannot be cached, if it cannot: what it
// compiles cannot be keyed, it links something the key cannot cover, or the
// file would not work copied out of the build directory that linked it.
std::optional<std::string>
    findReasonNotToCacheExecutable(const LibraryCacheInputs& inputs,
                                   const std::string& target,
                                   const LinkedLibraryKeys& libraryKeys);

// What the executable compiles, as a library's key covers it, and what decides
// its link: the key of every library it links, its link options and those of
// every target it links, the linker and its version, the libraries it links
// from the system and the target platform. Always covers the cache directory.
// Its record names the build of every library it links, so it is taken only
// where the same builds would be linked.
LibraryKey computeExecutableKey(const LibraryCacheInputs& inputs,
                                const std::string& target,
                                const LinkedLibraryKeys& libraryKeys);
} // namespace lockstep
