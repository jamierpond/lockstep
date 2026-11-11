#include "CompileFlags.h"
#include "PackageArguments.h"

#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <array>
#include <cctype>

namespace lockstep
{
namespace
{
// MSVC writes the debug information of these to a .pdb beside the objects,
// which the cache does not store, so a hit would link debug information that
// points into the first build directory.
constexpr auto separateProgramDatabaseFlags =
    std::array<std::string_view, 4> {"/Zi", "-Zi", "/ZI", "-ZI"};
constexpr auto separateProgramDatabaseFormats =
    std::array<std::string_view, 2> {"ProgramDatabase", "EditAndContinue"};

// With MSVC_DEBUG_INFORMATION_FORMAT unset, CMake (policy CMP0141) compiles
// these build types with ProgramDatabase.
constexpr auto buildTypesWithProgramDatabaseByDefault =
    std::array<std::string_view, 2> {"DEBUG", "RELWITHDEBINFO"};

// -gsplit-dwarf writes the debug information to a .dwo file beside each object,
// which the cache does not store; =single keeps it in the object.
constexpr auto splitDebugInformationFlag = std::string_view {"-gsplit-dwarf"};
constexpr auto splitDebugInformationInObjectFlag =
    std::string_view {"-gsplit-dwarf=single"};

// The flags that rewrite a path prefix in the object code: in __FILE__, in the
// debug information, or both. MSVC's /pathmap is left out, so a library
// compiled by MSVC keeps the cache directory in its key.
struct PathMappingFlag
{
    std::string_view prefix;
    bool mapsMacroPaths;
    bool mapsDebugInformationPaths;
};

constexpr auto pathMappingFlags =
    std::array<PathMappingFlag, 3> {{{"-ffile-prefix-map=", true, true},
                                     {"-fmacro-prefix-map=", true, false},
                                     {"-fdebug-prefix-map=", false, true}}};

constexpr auto linkTimeOptimisationFlag = std::string_view {"-flto"};
constexpr auto wholeProgramFlags = std::array<std::string_view, 2> {"/GL", "-GL"};
constexpr auto wholeProgramOffFlags =
    std::array<std::string_view, 2> {"/GL-", "-GL-"};
constexpr auto interproceduralOptimisationProperty =
    std::string_view {"INTERPROCEDURAL_OPTIMIZATION"};

// The compilers whose intermediate code only the version that wrote it reads.
constexpr auto sameVersionOnlyCompilerIds =
    std::array<std::string_view, 2> {"GNU", "MSVC"};

constexpr auto rootDirectory = std::string_view {"/"};
constexpr auto flagValueTerminators = std::string_view {" \t\n;\""};

constexpr auto targetFieldsHoldingCompileFlags = std::array<std::string_view, 3> {
    "COMPILE_OPTIONS", "COMPILE_FLAGS", "COMPILED_SOURCE_SETTINGS"};

constexpr auto configurationConditionStart = std::string_view {"$<$<CONFIG:"};
constexpr auto configurationConditionMiddle = std::string_view {">:"};
constexpr auto generatorExpressionEnd = std::string_view {">"};

bool isFlagCharacter(char character)
{
    return std::isalnum(static_cast<unsigned char>(character)) != 0;
}

std::optional<std::string_view>
    findSeparateProgramDatabaseFlag(std::string_view text)
{
    for (auto flag: separateProgramDatabaseFlags)
    {
        if (containsFlag(text, flag))
            return flag;
    }

    return std::nullopt;
}

// A "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>" generator expression.
struct ConfigurationCondition
{
    eacp::Vector<std::string> upperCaseConfigurations;
    std::string value;
};

std::optional<ConfigurationCondition>
    parseConfigurationCondition(const std::string& expression)
{
    if (!expression.starts_with(configurationConditionStart))
        return std::nullopt;

    if (!expression.ends_with(generatorExpressionEnd))
        return std::nullopt;

    auto middle = expression.find(configurationConditionMiddle);

    if (middle == std::string::npos)
        return std::nullopt;

    auto configurationsStart = configurationConditionStart.size();
    auto configurations = toUpperCase(
        expression.substr(configurationsStart, middle - configurationsStart));
    auto valueStart = middle + configurationConditionMiddle.size();
    auto valueLength =
        expression.size() - generatorExpressionEnd.size() - valueStart;

    return ConfigurationCondition {eacp::Strings::split(configurations, ','),
                                   expression.substr(valueStart, valueLength)};
}

std::string getDefaultDebugInformationFormat(const std::string& upperCaseBuildType)
{
    auto isProgramDatabaseByDefault =
        std::ranges::find(buildTypesWithProgramDatabaseByDefault, upperCaseBuildType)
        != buildTypesWithProgramDatabaseByDefault.end();

    if (isProgramDatabaseByDefault)
        return std::string {separateProgramDatabaseFormats.front()};

    return {};
}

// The format a MSVC_DEBUG_INFORMATION_FORMAT value gives `buildType`. Reads a
// plain value and $<$<CONFIG:...>:value>; any other generator expression comes
// back whole, so a format named anywhere in it still counts.
std::string evaluateDebugInformationFormat(const std::string& value,
                                           const std::string& buildType)
{
    auto upperCaseBuildType = toUpperCase(buildType);

    if (value.empty())
        return getDefaultDebugInformationFormat(upperCaseBuildType);

    auto condition = parseConfigurationCondition(value);

    if (!condition)
        return value;

    const auto& configurations = condition->upperCaseConfigurations;

    if (std::ranges::find(configurations, upperCaseBuildType)
        == configurations.end())
        return {};

    return condition->value;
}

bool isSeparateProgramDatabaseFormat(const std::string& format)
{
    auto isNamed = [&](std::string_view separateFormat)
    { return format.find(separateFormat) != std::string::npos; };

    return std::ranges::any_of(separateProgramDatabaseFormats, isNamed);
}

// COMPILE_OPTIONS, CMAKE_<LANG>_FLAGS and CMAKE_<LANG>_FLAGS_<CONFIG>.
bool isDirectoryFieldHoldingCompileFlags(const std::string& field)
{
    if (field == "COMPILE_OPTIONS")
        return true;

    return field.starts_with("CMAKE_") && field.find("_FLAGS") != std::string::npos;
}

// The text of every setting that puts flags on the target's compile line.
std::vector<std::string>
    collectCompileFlagTexts(const ConfigureDescription& description,
                            const std::string& target,
                            const RecordFields& directoryFields)
{
    auto texts = std::vector<std::string> {};

    for (auto field: targetFieldsHoldingCompileFlags)
        texts.push_back(findTargetField(description, target, field));

    for (const auto& [field, value]: directoryFields)
    {
        if (isDirectoryFieldHoldingCompileFlags(field))
            texts.push_back(value);
    }

    for (const auto& linked: collectLinkClosure(description, target))
        texts.push_back(
            findTargetField(description, linked, "INTERFACE_COMPILE_OPTIONS"));

    return texts;
}

bool hasSplitDebugInformationFlag(const std::string& text)
{
    auto withoutSingleFile =
        replaceAll(text, splitDebugInformationInObjectFlag, std::string_view {});
    return containsFlag(withoutSingleFile, splitDebugInformationFlag);
}

bool isFlagStart(std::string_view text, std::size_t position)
{
    if (position == 0)
        return true;

    return flagValueTerminators.find(text[position - 1]) != std::string_view::npos;
}

std::string removeTrailingSlash(std::string path)
{
    if (path.size() > 1 && path.ends_with('/'))
        path.pop_back();

    return path;
}

// The old prefixes of every `<flag><old>=<new>` in `text`.
std::vector<std::string> findMappedPrefixes(std::string_view text,
                                            std::string_view flag)
{
    auto prefixes = std::vector<std::string> {};

    for (auto position = text.find(flag); position != std::string_view::npos;
         position = text.find(flag, position + 1))
    {
        if (!isFlagStart(text, position))
            continue;

        auto valueStart = position + flag.size();
        auto valueEnd = text.find_first_of(flagValueTerminators, valueStart);
        auto value = text.substr(valueStart, valueEnd - valueStart);
        auto oldPrefix = value.substr(0, value.find('='));
        prefixes.push_back(
            removeTrailingSlash(toForwardSlashes(std::string {oldPrefix})));
    }

    return prefixes;
}

bool mapsDirectory(std::string_view text,
                   std::string_view flag,
                   const std::string& directory)
{
    auto isParentOfDirectory = [&](const std::string& prefix)
    {
        return prefix == rootDirectory
               || (!prefix.empty() && isInsideDirectory(directory, prefix));
    };

    return std::ranges::any_of(findMappedPrefixes(text, flag), isParentOfDirectory);
}

bool mapsMacroAndDebugInformationPaths(const std::vector<std::string>& texts,
                                       const std::string& directory)
{
    auto mapsMacroPaths = false;
    auto mapsDebugInformationPaths = false;

    for (const auto& text: texts)
    {
        for (const auto& flag: pathMappingFlags)
        {
            if (!mapsDirectory(text, flag.prefix, directory))
                continue;

            mapsMacroPaths = mapsMacroPaths || flag.mapsMacroPaths;
            mapsDebugInformationPaths =
                mapsDebugInformationPaths || flag.mapsDebugInformationPaths;
        }
    }

    return mapsMacroPaths && mapsDebugInformationPaths;
}

// The flags on every compile of the target, whatever its language. Per-source
// flags are left out: they cover some sources only.
std::vector<std::string>
    collectFlagTextsOfEveryCompile(const ConfigureDescription& description,
                                   const std::string& target,
                                   const RecordFields& directoryFields)
{
    auto texts = std::vector<std::string> {
        findTargetField(description, target, "COMPILE_OPTIONS"),
        findTargetField(description, target, "COMPILE_FLAGS"),
        findField(directoryFields, "COMPILE_OPTIONS")};

    for (const auto& linked: collectLinkClosure(description, target))
        texts.push_back(
            findTargetField(description, linked, "INTERFACE_COMPILE_OPTIONS"));

    return texts;
}

bool hasWholeProgramFlag(std::string text)
{
    for (auto flag: wholeProgramOffFlags)
        text = replaceAll(text, flag, {});

    auto isInText = [&](std::string_view flag) { return containsFlag(text, flag); };
    return std::ranges::any_of(wholeProgramFlags, isInText);
}

bool asksForInterproceduralOptimisation(const ConfigureDescription& description,
                                        const std::string& target,
                                        const RecordFields& directoryFields)
{
    auto property = std::string {interproceduralOptimisationProperty};
    auto configuration = toUpperCase(findField(directoryFields, "CMAKE_BUILD_TYPE"));

    return isCMakeTrue(findTargetField(description, target, property))
           || isCMakeTrue(
               findTargetField(description, target, property + "_" + configuration));
}

bool isReadBySameCompilerVersionOnly(
    const RecordFields& directoryFields,
    const std::vector<std::string>& compiledLanguages)
{
    for (const auto& language: compiledLanguages)
    {
        auto compilerId =
            findField(directoryFields, "CMAKE_" + language + "_COMPILER_ID");

        if (std::ranges::find(sameVersionOnlyCompilerIds, compilerId)
            != sameVersionOnlyCompilerIds.end())
            return true;
    }

    return false;
}
} // namespace

bool containsFlag(std::string_view text, std::string_view flag)
{
    for (auto position = text.find(flag); position != std::string_view::npos;
         position = text.find(flag, position + 1))
    {
        auto end = position + flag.size();
        auto isWordStart = position == 0 || !isFlagCharacter(text[position - 1]);
        auto isWordEnd = end == text.size() || !isFlagCharacter(text[end]);

        if (isWordStart && isWordEnd)
            return true;
    }

    return false;
}

bool compilesForMsvcAbi(const RecordFields& directoryFields,
                        const std::vector<std::string>& compiledLanguages)
{
    for (const auto& language: compiledLanguages)
    {
        auto prefix = "CMAKE_" + language + "_";

        if (findField(directoryFields, prefix + "COMPILER_ID") == "MSVC"
            || findField(directoryFields, prefix + "SIMULATE_ID") == "MSVC")
            return true;
    }

    return false;
}

std::optional<std::string>
    findReasonInDebugInformation(const ConfigureDescription& description,
                                 const std::string& target)
{
    auto directory = findTargetField(description, target, "SOURCE_DIR");
    const auto& directoryFields = findSubject(description.directories, directory);
    auto compiledLanguages =
        splitCMakeList(findTargetField(description, target, "COMPILED_LANGUAGES"));

    if (!compilesForMsvcAbi(directoryFields, compiledLanguages))
        return std::nullopt;

    for (const auto& text:
         collectCompileFlagTexts(description, target, directoryFields))
    {
        if (auto flag = findSeparateProgramDatabaseFlag(text))
            return "its compile flags hold " + std::string {*flag};
    }

    auto setting =
        findTargetField(description, target, "MSVC_DEBUG_INFORMATION_FORMAT");
    auto buildType = findField(directoryFields, "CMAKE_BUILD_TYPE");
    auto format = evaluateDebugInformationFormat(setting, buildType);

    if (!isSeparateProgramDatabaseFormat(format))
        return std::nullopt;

    if (setting.empty())
        return "its MSVC_DEBUG_INFORMATION_FORMAT is unset, which CMake compiles as "
               + format + " in a " + buildType + " build";

    return "its MSVC_DEBUG_INFORMATION_FORMAT is " + format;
}

std::optional<std::string>
    findReasonInSplitDebugInformation(const ConfigureDescription& description,
                                      const std::string& target)
{
    auto directory = findTargetField(description, target, "SOURCE_DIR");
    const auto& directoryFields = findSubject(description.directories, directory);

    for (const auto& text:
         collectCompileFlagTexts(description, target, directoryFields))
    {
        if (hasSplitDebugInformationFlag(text))
            return "its compile flags hold "
                   + std::string {splitDebugInformationFlag}
                   + ", which writes its debug information to .dwo files beside the "
                     "objects, which the cache does not store";
    }

    return std::nullopt;
}

IntermediateCode findIntermediateCode(const ConfigureDescription& description,
                                      const std::string& target)
{
    auto directory = findTargetField(description, target, "SOURCE_DIR");
    const auto& directoryFields = findSubject(description.directories, directory);
    auto compiledLanguages =
        splitCMakeList(findTargetField(description, target, "COMPILED_LANGUAGES"));
    auto texts = collectCompileFlagTexts(description, target, directoryFields);
    auto isMsvcAbi = compilesForMsvcAbi(directoryFields, compiledLanguages);
    auto hasWholeProgram =
        isMsvcAbi && std::ranges::any_of(texts, hasWholeProgramFlag);
    auto hasLinkTimeOptimisation = [](const std::string& text)
    { return containsFlag(text, linkTimeOptimisationFlag); };

    auto isOptimisedAtLink =
        hasWholeProgram || std::ranges::any_of(texts, hasLinkTimeOptimisation)
        || asksForInterproceduralOptimisation(description, target, directoryFields);

    if (!isOptimisedAtLink)
        return IntermediateCode::none;

    if (hasWholeProgram
        || isReadBySameCompilerVersionOnly(directoryFields, compiledLanguages))
        return IntermediateCode::readableBySameCompilerVersionOnly;

    return IntermediateCode::readableByNewerCompilers;
}

bool isCacheDirectoryMappedAway(const LibraryCacheInputs& inputs,
                                const std::string& target)
{
    auto directory = findTargetField(inputs.description, target, "SOURCE_DIR");
    const auto& directoryFields =
        findSubject(inputs.description.directories, directory);
    auto compiledLanguages = splitCMakeList(
        findTargetField(inputs.description, target, "COMPILED_LANGUAGES"));
    auto configuration = toUpperCase(findField(directoryFields, "CMAKE_BUILD_TYPE"));
    auto isMappedForLanguage = [&](const std::string& language)
    {
        auto texts = collectFlagTextsOfEveryCompile(
            inputs.description, target, directoryFields);
        auto languageFlags = "CMAKE_" + language + "_FLAGS";
        texts.push_back(findField(directoryFields, languageFlags));
        texts.push_back(
            findField(directoryFields, languageFlags + "_" + configuration));
        return mapsMacroAndDebugInformationPaths(texts, inputs.paths.cacheDirectory);
    };

    return !compiledLanguages.empty()
           && std::ranges::all_of(compiledLanguages, isMappedForLanguage);
}
} // namespace lockstep
