#pragma once

#include "BuiltWith.h"
#include "ConfigureDescription.h"

#include <string>
#include <string_view>
#include <vector>

namespace lockstep
{
struct LibraryKey
{
    std::string key;

    // What the key is the SHA-256 of.
    std::string inputs;

    // What this machine's build of it is built with, and that record's id.
    BuiltWith builtWith;
    std::string buildId;
};

// The fields that cannot change a target's output: they stay out of its key,
// so projects that differ only in them share the entry.
class KeyFieldFilter
{
public:
    void leaveOut(std::string_view field) { leftOutNames.emplace_back(field); }

    void leaveOutPrefix(std::string prefix)
    {
        leftOutPrefixes.push_back(std::move(prefix));
    }

    void leaveOutSuffix(std::string suffix)
    {
        leftOutSuffixes.push_back(std::move(suffix));
    }

    bool isLeftOut(const std::string& field) const;

private:
    std::vector<std::string> leftOutNames;
    std::vector<std::string> leftOutPrefixes;
    std::vector<std::string> leftOutSuffixes;
};

// The text a key is the SHA-256 of, with the build and cache directories
// replaced by fixed words.
class KeyText
{
public:
    explicit KeyText(const MachinePaths& pathsToUse);

    void addLine(std::string_view label,
                 const std::string& name,
                 const std::string& value);

    // The cache directory's own path, the one value that stays machine-specific.
    void addCacheDirectory();

    void addFields(std::string_view label,
                   const RecordFields& fields,
                   const KeyFieldFilter& filter);

    const std::string& getText() const { return text; }

private:
    MachinePaths paths;
    std::string text;
};

// The target's package hash, its fields and the variables its package's CMake
// names.
void addTarget(KeyText& text,
               const LibraryCacheInputs& inputs,
               const std::string& target,
               const KeyFieldFilter& filter);

// addTarget for the target the key is for, leaving out the fields that change
// only how its consumers see it.
void addOwnTarget(KeyText& text,
                  const LibraryCacheInputs& inputs,
                  const std::string& target,
                  const KeyFieldFilter& filter);

// The fields left out of the key of a library's object code: the settings of
// languages it compiles no source in, and every link setting.
KeyFieldFilter createKeyFieldFilter(const ConfigureDescription& description,
                                    const std::string& target);

// Leaves out exactly the settings that change only what a link writes, so its
// isLeftOut says whether a field is one.
KeyFieldFilter createLinkSettingFilter(const ConfigureDescription& description);

// The fields left out of an executable's key, which covers its link: only
// those no compile or link reads.
KeyFieldFilter createLinkKeyFieldFilter(const ConfigureDescription& description,
                                        const std::string& target);
} // namespace lockstep
