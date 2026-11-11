#pragma once

#include "ConfigureDescription.h"
#include "KeyText.h"
#include "LibraryStore.h"

#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace lockstep
{
// Raised whenever what goes into a key or the store's files change meaning, so
// no older entry hits and the store reads no older file.
constexpr auto libraryCacheFormatVersion = 6;

// The kind of entry a target of this TYPE is stored as, or nullopt for a type
// the cache does not store.
std::optional<CachedTargetKind> findCachedTargetKind(const RecordFields& fields);

// "static library", "executable" or "object library".
std::string getCachedTargetKindName(CachedTargetKind kind);

// "static libraries", "executables" or "object libraries".
std::string getCachedTargetKindPluralName(CachedTargetKind kind);

// The first target `target` links, through its link closure, that is not
// imported and not from a locked package.
std::optional<std::string>
    findLinkedTargetOutsideCachedPackages(const LibraryCacheInputs& inputs,
                                          const std::string& target);

// Why the target's compiled code cannot be keyed: its package is not locked,
// its sources are out of the key's reach, or its debug information is written
// beside the objects.
std::optional<std::string>
    findReasonNotToCacheCompiledCode(const LibraryCacheInputs& inputs,
                                     const std::string& target);

// Why a static or object library cannot be cached, if it cannot.
std::optional<std::string> findReasonNotToCache(const LibraryCacheInputs& inputs,
                                                const std::string& target);

// The format and kind lines every key starts with.
void addFormat(KeyText& text, CachedTargetKind kind);

// The key of a static or object library. The package sources live in the
// cache directory, and object code can hold their absolute paths (__FILE__,
// debug information), so the key covers the cache directory unless the
// target's flags map it away with -ffile-prefix-map, or with both
// -fmacro-prefix-map and -fdebug-prefix-map.
LibraryKey computeLibraryKey(const LibraryCacheInputs& inputs,
                             const std::string& target);

std::set<std::string> collectUpperCaseIdentifiers(std::string_view text);

std::set<std::string>
    collectPackageVariableNames(const eacp::FilePath& sourceDirectory);

// The command's arguments, with the words of each @<file> response file in its
// place.
std::vector<std::string> expandResponseFiles(const std::vector<std::string>& command,
                                             const eacp::FilePath& workingDirectory);

// True when `file` is one of the command's arguments, or is named in a
// response file (@file) among them, so the command really reads it.
bool isCommandInput(const std::vector<std::string>& command,
                    const eacp::FilePath& file,
                    const eacp::FilePath& workingDirectory);
} // namespace lockstep
