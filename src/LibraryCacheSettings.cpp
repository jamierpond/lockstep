#include "LibraryCacheSettings.h"
#include "ByteSize.h"
#include "PackageArguments.h"
#include "ProjectSettings.h"

#include <array>
#include <stdexcept>

namespace lockstep
{
namespace
{
struct SourceName
{
    SettingSource source;
    std::string_view description;
};

constexpr auto sourceNames = std::array<SourceName, 4> {
    {{SettingSource::buildDirectory, "a -D on the build directory"},
     {SettingSource::environment, "the environment"},
     {SettingSource::projectFile, "lockstep.json"},
     {SettingSource::defaultValue, "the default"}}};

std::optional<std::string>
    findValue(const std::map<std::string, std::string>& values,
              std::string_view variable)
{
    auto found = values.find(std::string {variable});

    if (found == values.end())
        return std::nullopt;

    return found->second;
}

SettingValue resolveSetting(const SettingSources& sources,
                            const SettingDescription& description)
{
    if (auto value = findValue(sources.cacheVariables, description.variable))
        return {*value, SettingSource::buildDirectory};

    if (auto value = findValue(sources.environment, description.variable);
        value && !value->empty())
        return {*value, SettingSource::environment};

    if (auto value = findValue(sources.projectFile, description.variable))
        return {*value, SettingSource::projectFile};

    return {std::string {description.defaultText}, SettingSource::defaultValue};
}

std::optional<VersionRange> parseRangeSetting(const SettingValue& value,
                                              std::string_view variable)
{
    if (value.text.empty())
        return std::nullopt;

    try
    {
        return parseVersionRange(value.text);
    }
    catch (const std::invalid_argument& error)
    {
        throw std::runtime_error(std::string {variable} + " from "
                                 + describeSettingSource(value.source) + ": "
                                 + error.what());
    }
}
std::uint64_t parseSizeSetting(const SettingValue& value, std::string_view variable)
{
    try
    {
        return parseByteSize(value.text);
    }
    catch (const std::invalid_argument& error)
    {
        throw std::runtime_error(std::string {variable} + " from "
                                 + describeSettingSource(value.source) + ": "
                                 + error.what());
    }
}
} // namespace

const std::vector<SettingDescription>& getSettingDescriptions()
{
    static const auto descriptions = std::vector<SettingDescription> {
        {"LOCKSTEP_LIBRARY_CACHE", "enabled", &LibraryCacheSettings::enabled, "OFF"},
        {"LOCKSTEP_LIBRARY_CACHE_LOCATION",
         "location",
         &LibraryCacheSettings::location,
         ""},
        {"LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE",
         "compilerRange",
         &LibraryCacheSettings::compilerRange,
         ""},
        {"LOCKSTEP_LIBRARY_CACHE_SDK_RANGE",
         "sdkRange",
         &LibraryCacheSettings::sdkRange,
         ""},
        {"LOCKSTEP_LIBRARY_CACHE_MAX_SIZE",
         "maxSize",
         &LibraryCacheSettings::maxSize,
         "5 GiB"},
        {"LOCKSTEP_LIBRARY_CACHE_LOCATION_MAX_SIZE",
         "locationMaxSize",
         &LibraryCacheSettings::locationMaxSize,
         "20 GiB"}};

    return descriptions;
}

SettingSources
    createSettingSources(const eacp::Vector<std::string>& cacheVariables,
                         const std::map<std::string, std::string>& environment,
                         const eacp::FilePath& projectSettingsFile)
{
    auto sources = SettingSources {};
    sources.environment = environment;
    sources.projectFile =
        getLibraryCacheSettingValues(readProjectSettings(projectSettingsFile));

    for (const auto& definition: cacheVariables)
    {
        auto equals = definition.find('=');

        if (equals == std::string::npos)
            throw std::runtime_error("--cache-variable takes <name>=<value>, not "
                                     + definition);

        sources.cacheVariables[definition.substr(0, equals)] =
            definition.substr(equals + 1);
    }

    return sources;
}

LibraryCacheSettings resolveLibraryCacheSettings(const SettingSources& sources)
{
    auto settings = LibraryCacheSettings {};

    for (const auto& description: getSettingDescriptions())
        settings.*description.member = resolveSetting(sources, description);

    return settings;
}

std::vector<std::string> getSettingVariableNames()
{
    auto names = std::vector<std::string> {};

    for (const auto& description: getSettingDescriptions())
        names.emplace_back(description.variable);

    return names;
}

bool isLibraryCacheEnabled(const LibraryCacheSettings& settings)
{
    return isCMakeTrue(settings.enabled.text);
}

ToolchainRanges parseToolchainRanges(const LibraryCacheSettings& settings)
{
    return {
        parseRangeSetting(settings.compilerRange,
                          "LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE"),
        parseRangeSetting(settings.sdkRange, "LOCKSTEP_LIBRARY_CACHE_SDK_RANGE")};
}

LibraryCacheSizeLimits parseSizeLimits(const LibraryCacheSettings& settings)
{
    return {parseSizeSetting(settings.maxSize, "LOCKSTEP_LIBRARY_CACHE_MAX_SIZE"),
            parseSizeSetting(settings.locationMaxSize,
                             "LOCKSTEP_LIBRARY_CACHE_LOCATION_MAX_SIZE")};
}

std::string describeSettingSource(SettingSource source)
{
    for (const auto& name: sourceNames)
    {
        if (name.source == source)
            return std::string {name.description};
    }

    return {};
}
} // namespace lockstep
