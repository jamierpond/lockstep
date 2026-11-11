#pragma once

#include "BuiltWith.h"

#include <eacp/Core/Utils/Containers.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace lockstep
{
enum class SettingSource
{
    buildDirectory,
    environment,
    projectFile,
    defaultValue
};

struct SettingValue
{
    std::string text;
    SettingSource source = SettingSource::defaultValue;
};

// The library cache's settings, each from the first source that sets it: a -D
// on the build directory, the environment variable, the project's
// lockstep.json, or the default.
struct LibraryCacheSettings
{
    SettingValue enabled;
    SettingValue location;
    SettingValue compilerRange;
    SettingValue sdkRange;
    SettingValue maxSize;
    SettingValue locationMaxSize;
};

// What each source says, by the setting's variable name, such as
// LOCKSTEP_LIBRARY_CACHE_LOCATION. A cache variable counts even when empty,
// since -DLOCKSTEP_LIBRARY_CACHE_LOCATION= turns the location off; an empty
// environment variable does not.
struct SettingSources
{
    std::map<std::string, std::string> cacheVariables;
    std::map<std::string, std::string> environment;
    std::map<std::string, std::string> projectFile;
};

// One setting: its variable, its key in lockstep.json's "libraryCache", the
// member it fills and its default.
struct SettingDescription
{
    std::string_view variable;
    std::string_view projectFileKey;
    SettingValue LibraryCacheSettings::* member;
    std::string_view defaultText;
};

const std::vector<SettingDescription>& getSettingDescriptions();

// The sources from the module's --cache-variable=<name>=<value> definitions and
// the environment variables read at start-up.
SettingSources
    createSettingSources(const eacp::Vector<std::string>& cacheVariables,
                         const std::map<std::string, std::string>& environment);

LibraryCacheSettings resolveLibraryCacheSettings(const SettingSources& sources);

// Every setting's variable name, which the environment is read for.
std::vector<std::string> getSettingVariableNames();

bool isLibraryCacheEnabled(const LibraryCacheSettings& settings);

// Throws std::runtime_error naming the setting and where it came from when a
// range is not one.
ToolchainRanges parseToolchainRanges(const LibraryCacheSettings& settings);

// "a -D on the build directory", "the environment", "lockstep.json" or "the
// default".
std::string describeSettingSource(SettingSource source);
} // namespace lockstep
