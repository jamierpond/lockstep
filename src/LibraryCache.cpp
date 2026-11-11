#include "LibraryCache.h"
#include "PackageArguments.h"
#include "Sha256.h"

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>

namespace lockstep
{
namespace
{
constexpr auto recordPartCount = 4;
constexpr auto buildDirectoryWord = std::string_view {"<build>"};
constexpr auto cacheDirectoryWord = std::string_view {"<cache>"};

// CMake writes "::@(<directory id>)" into a link list that another directory
// added to; the id is a pointer, so it changes on every configure.
constexpr auto directoryMarkerStart = std::string_view {"::@("};

// try_compile names its scratch project cmTC_<random hex>, and a package that
// keeps try_compile's output in a variable carries that name into the key.
constexpr auto tryCompileProjectPrefix = std::string_view {"cmTC_"};

// The standard's _REQUIRED only decides whether CMake stops when the compiler
// lacks the standard; a build that gets past that compiles with the same flags.
constexpr auto fieldsLeftOutOfKey = std::array<std::string_view, 3> {
    "EXISTING_LINK_LAUNCHER", "C_STANDARD_REQUIRED", "CXX_STANDARD_REQUIRED"};

// SYSTEM changes only how the target's consumers include its headers.
constexpr auto ownFieldsLeftOutOfKey = std::array<std::string_view, 1> {"SYSTEM"};

// Ninja and Unix Makefiles give a library the same compile and archive
// commands, except that Ninja adds a module map (@<object>.modmap) to the C++
// sources it scans for modules, and Unix Makefiles cannot scan.
constexpr auto generatorSetting = std::string_view {"CMAKE_GENERATOR"};

constexpr auto appleOnlySettings =
    std::array<std::string_view, 5> {"CMAKE_OSX_SYSROOT",
                                     "CMAKE_OSX_DEPLOYMENT_TARGET",
                                     "CMAKE_OSX_ARCHITECTURES",
                                     "APPLE_SDK_VERSION",
                                     "APPLE_SDK_BUILD_VERSION"};

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

constexpr auto rootDirectory = std::string_view {"/"};
constexpr auto flagValueTerminators = std::string_view {" \t\n;\""};
constexpr auto cacheDirectoryKeyLabel = std::string_view {"machine"};
constexpr auto cacheDirectoryKeyName = "cache-directory";

constexpr auto targetFieldsHoldingCompileFlags = std::array<std::string_view, 3> {
    "COMPILE_OPTIONS", "COMPILE_FLAGS", "COMPILED_SOURCE_SETTINGS"};

// CMake sets these in every function call, and a package's directory inherits
// them from the lockstep_add_package (or FetchContent_MakeAvailable) call that
// added it, so their values are the text of that call. ARGV0, ARGV1, ... too.
constexpr auto functionArgumentVariables =
    std::array<std::string_view, 3> {"ARGC", "ARGN", "ARGV"};
constexpr auto numberedFunctionArgumentPrefix = std::string_view {"ARGV"};

constexpr auto configurationConditionStart = std::string_view {"$<$<CONFIG:"};
constexpr auto configurationConditionMiddle = std::string_view {">:"};
constexpr auto generatorExpressionEnd = std::string_view {">"};

std::string unescapeRecordValue(std::string_view value)
{
    auto text = std::string {};

    for (auto index = std::size_t {0}; index < value.size(); ++index)
    {
        if (value[index] != '\\' || index + 1 == value.size())
        {
            text += value[index];
            continue;
        }

        auto escaped = value[++index];

        if (escaped == 'n')
            text += '\n';
        else if (escaped == 'r')
            text += '\r';
        else if (escaped == 't')
            text += '\t';
        else
            text += escaped;
    }

    return text;
}

std::string replaceAll(std::string text, std::string_view from, std::string_view to)
{
    if (from.empty())
        return text;

    for (auto position = text.find(from); position != std::string::npos;
         position = text.find(from, position + to.size()))
        text.replace(position, from.size(), to);

    return text;
}

std::string removeDirectoryIds(std::string text)
{
    for (auto position = text.find(directoryMarkerStart);
         position != std::string::npos;
         position = text.find(directoryMarkerStart, position + 1))
    {
        auto idStart = position + directoryMarkerStart.size();
        auto closing = text.find(')', idStart);

        if (closing != std::string::npos)
            text.erase(idStart, closing - idStart);
    }

    return text;
}

std::string removeTryCompileProjectNames(std::string text)
{
    for (auto position = text.find(tryCompileProjectPrefix);
         position != std::string::npos;
         position = text.find(tryCompileProjectPrefix, position + 1))
    {
        auto idStart = position + tryCompileProjectPrefix.size();
        auto idEnd = idStart;

        while (idEnd < text.size()
               && std::isxdigit(static_cast<unsigned char>(text[idEnd])))
            ++idEnd;

        text.erase(idStart, idEnd - idStart);
    }

    return text;
}

std::string makeMachineIndependent(const std::string& text,
                                   const MachinePaths& paths)
{
    auto independent = replaceAll(text, paths.buildDirectory, buildDirectoryWord);
    independent = replaceAll(independent, paths.cacheDirectory, cacheDirectoryWord);
    return removeTryCompileProjectNames(removeDirectoryIds(independent));
}

std::vector<std::string> splitCMakeList(const std::string& list)
{
    auto items = std::vector<std::string> {};

    for (const auto& item: eacp::Strings::split(list, ';'))
    {
        if (!item.empty())
            items.push_back(item);
    }

    return items;
}

bool isInsideDirectory(const std::string& path, const std::string& directory)
{
    auto normalizedPath = toForwardSlashes(path);
    auto normalizedDirectory = toForwardSlashes(directory);
    return normalizedPath == normalizedDirectory
           || normalizedPath.starts_with(normalizedDirectory + "/");
}

// The fields that cannot change a target's object code: they stay out of its
// key, so projects that differ only in them share the library.
class KeyFieldFilter
{
public:
    void leaveOut(std::string_view field) { leftOutNames.emplace_back(field); }

    void leaveOutPrefix(std::string prefix)
    {
        leftOutPrefixes.push_back(std::move(prefix));
    }

    bool isLeftOut(const std::string& field) const
    {
        auto isPrefixOfField = [&](const std::string& prefix)
        { return field.starts_with(prefix); };

        return std::ranges::find(leftOutNames, field) != leftOutNames.end()
               || std::ranges::any_of(leftOutPrefixes, isPrefixOfField);
    }

private:
    std::vector<std::string> leftOutNames;
    std::vector<std::string> leftOutPrefixes;
};

std::string findTargetField(const ConfigureDescription& description,
                            const std::string& target,
                            std::string_view field)
{
    auto found = description.targets.find(target);

    if (found == description.targets.end())
        return {};

    return findField(found->second, field);
}

bool isUsedThroughTargetObjects(const ConfigureDescription& description,
                                const std::string& target)
{
    auto expression = "$<TARGET_OBJECTS:" + target + ">";

    for (const auto& [name, fields]: description.targets)
    {
        for (const auto& [field, value]: fields)
        {
            if (value.find(expression) != std::string::npos)
                return true;
        }
    }

    return false;
}

// What about the target's own sources the cache cannot handle.
std::optional<std::string>
    findReasonInSources(const ConfigureDescription& description,
                        const std::string& target)
{
    if (auto source =
            findTargetField(description, target, "GENERATOR_EXPRESSION_SOURCE");
        !source.empty())
        return "its SOURCES hold the generator expression " + source;

    if (isUsedThroughTargetObjects(description, target))
        return "another target takes its object files through $<TARGET_OBJECTS:"
               + target + ">";

    if (findTargetField(description, target, "STUB_LANGUAGE").empty())
        return "it compiles no C or C++ source";

    for (auto generator: {"AUTOMOC", "AUTOUIC", "AUTORCC"})
    {
        if (isCMakeTrue(findTargetField(description, target, generator)))
            return std::string {"it runs "} + generator
                   + ", whose generated sources the key does not cover";
    }

    if (!findTargetField(description, target, "CXX_MODULE_SETS").empty())
        return "it has C++ module sets";

    return std::nullopt;
}

std::optional<std::string>
    findLinkedTargetOutsideCachedPackages(const LibraryCacheInputs& inputs,
                                          const std::string& target)
{
    for (const auto& linked: collectLinkClosure(inputs.description, target))
    {
        if (isCMakeTrue(findTargetField(inputs.description, linked, "IMPORTED")))
            continue;

        auto directory = findTargetField(inputs.description, linked, "SOURCE_DIR");
        auto owner = findOwningPackage(inputs.packages, directory);

        if (!owner || owner->hash.empty())
            return linked;
    }

    return std::nullopt;
}

class KeyText
{
public:
    explicit KeyText(const MachinePaths& pathsToUse)
        : paths(pathsToUse)
    {
    }

    void addLine(std::string_view label,
                 const std::string& name,
                 const std::string& value)
    {
        auto independent = makeMachineIndependent(value, paths);
        text += std::string {label} + " " + name + "="
                + replaceAll(independent, "\n", "\\n") + "\n";
    }

    // For the one value that must stay machine-specific.
    void addLineKeepingPaths(std::string_view label,
                             const std::string& name,
                             const std::string& value)
    {
        text += std::string {label} + " " + name + "=" + value + "\n";
    }

    void addFields(std::string_view label,
                   const RecordFields& fields,
                   const KeyFieldFilter& filter)
    {
        for (const auto& [field, value]: fields)
        {
            if (!filter.isLeftOut(field))
                addLine(label, field, value);
        }
    }

    const std::string& getText() const { return text; }

private:
    MachinePaths paths;
    std::string text;
};

const RecordFields& findSubject(const std::map<std::string, RecordFields>& subjects,
                                const std::string& subject)
{
    static const auto none = RecordFields {};
    auto found = subjects.find(subject);

    if (found == subjects.end())
        return none;

    return found->second;
}

std::string toUpperCase(std::string text)
{
    for (auto& character: text)
        character =
            static_cast<char>(std::toupper(static_cast<unsigned char>(character)));

    return text;
}

bool isFlagCharacter(char character)
{
    return std::isalnum(static_cast<unsigned char>(character)) != 0;
}

// True when `flag` stands as a whole word in `text`, not as part of a longer
// flag or path.
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

// Why MSVC would write the target's debug information to a separate .pdb, if
// it would.
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

bool hasSplitDebugInformationFlag(const std::string& text)
{
    auto withoutSingleFile =
        replaceAll(text, splitDebugInformationInObjectFlag, std::string_view {});
    return containsFlag(withoutSingleFile, splitDebugInformationFlag);
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

// True when every language the target compiles has its cache directory paths
// mapped away, in __FILE__ and in the debug information.
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

bool isFunctionArgumentVariable(std::string_view name)
{
    if (std::ranges::find(functionArgumentVariables, name)
        != functionArgumentVariables.end())
        return true;

    if (!name.starts_with(numberedFunctionArgumentPrefix))
        return false;

    auto number = name.substr(numberedFunctionArgumentPrefix.size());
    auto isDigit = [](char character)
    { return std::isdigit(static_cast<unsigned char>(character)) != 0; };

    return !number.empty() && std::ranges::all_of(number, isDigit);
}

// The variables in the target's directory that its package's CMake names.
void addPackageVariables(KeyText& text,
                         const LibraryCacheInputs& inputs,
                         const std::string& target)
{
    auto directory = findTargetField(inputs.description, target, "SOURCE_DIR");
    auto owner = findOwningPackage(inputs.packages, directory);

    if (!owner)
        return;

    auto names = inputs.variableNames.find(owner->hash);

    if (names == inputs.variableNames.end())
        return;

    for (const auto& [name, value]:
         findSubject(inputs.description.variables, directory))
    {
        if (names->second.contains(name) && !isFunctionArgumentVariable(name))
            text.addLine("variable", name, value);
    }
}

void addTarget(KeyText& text,
               const LibraryCacheInputs& inputs,
               const std::string& target,
               const KeyFieldFilter& filter)
{
    auto directory = findTargetField(inputs.description, target, "SOURCE_DIR");
    auto owner = findOwningPackage(inputs.packages, directory);

    text.addLine("target", target, owner ? owner->hash : std::string {});
    text.addFields("field", findSubject(inputs.description.targets, target), filter);
    addPackageVariables(text, inputs, target);
}

// A target never runs the compiler of a language it has no source in, so that
// language's CMAKE_<LANG>_ settings stay out of its key; nor do the Apple SDK
// settings when the target system is not Apple's.
void leaveOutLanguagesNotCompiled(KeyFieldFilter& filter,
                                  const ConfigureDescription& description,
                                  const std::string& target)
{
    auto compiledLanguages =
        splitCMakeList(findTargetField(description, target, "COMPILED_LANGUAGES"));
    auto isCompiled = [&](const std::string& language)
    {
        return std::ranges::find(compiledLanguages, language)
               != compiledLanguages.end();
    };

    for (const auto& language:
         splitCMakeList(findField(description.globals, "ENABLED_LANGUAGES")))
    {
        if (!isCompiled(language))
            filter.leaveOutPrefix("CMAKE_" + language + "_");
    }
}

// Unset, CXX_SCAN_FOR_MODULES scans C++20 sources wherever the generator can.
bool isScannedForModules(const RecordFields& targetFields)
{
    auto compiledLanguages =
        splitCMakeList(findField(targetFields, "COMPILED_LANGUAGES"));

    if (std::ranges::find(compiledLanguages, "CXX") == compiledLanguages.end())
        return false;

    auto scanning = findField(targetFields, "CXX_SCAN_FOR_MODULES");
    return scanning.empty() || !isCMakeFalse(scanning);
}

KeyFieldFilter createKeyFieldFilter(const ConfigureDescription& description,
                                    const std::string& target)
{
    auto filter = KeyFieldFilter {};
    auto directory = findTargetField(description, target, "SOURCE_DIR");
    auto isAppleSystem = isCMakeTrue(
        findField(findSubject(description.directories, directory), "APPLE"));

    for (auto field: fieldsLeftOutOfKey)
        filter.leaveOut(field);

    for (auto setting: appleOnlySettings)
    {
        if (!isAppleSystem)
            filter.leaveOut(setting);
    }

    if (!isScannedForModules(findSubject(description.targets, target)))
        filter.leaveOut(generatorSetting);

    leaveOutLanguagesNotCompiled(filter, description, target);
    return filter;
}

std::vector<std::string> readResponseFile(const eacp::FilePath& path)
{
    auto words = std::vector<std::string> {};

    if (!eacp::Files::isFile(path))
        return words;

    auto word = std::string {};
    auto isQuoted = false;

    for (auto character: eacp::Files::readFile(path))
    {
        if (character == '"')
        {
            isQuoted = !isQuoted;
            continue;
        }

        if (!isQuoted && std::isspace(static_cast<unsigned char>(character)))
        {
            if (!word.empty())
                words.push_back(word);

            word.clear();
            continue;
        }

        word += character;
    }

    if (!word.empty())
        words.push_back(word);

    return words;
}

bool isSameFile(const std::filesystem::path& first,
                const std::filesystem::path& second)
{
    auto error = std::error_code {};
    return std::filesystem::equivalent(first, second, error);
}
} // namespace

std::string toForwardSlashes(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

ConfigureDescription parseConfigureDescription(std::string_view text)
{
    auto description = ConfigureDescription {};

    for (auto line: eacp::Strings::split(text, '\n'))
    {
        // CMake's file(WRITE) ends each line with \r\n on Windows; the module
        // escapes every carriage return in a value, so this one is the line's.
        if (line.ends_with('\r'))
            line.pop_back();

        auto parts = eacp::Strings::split(line, '\t', recordPartCount);

        if (parts.size() != recordPartCount)
            continue;

        auto field = std::pair {parts[2], unescapeRecordValue(parts[3])};
        const auto& kind = parts[0];

        if (kind == "global")
            description.globals.push_back(field);
        else if (kind == "target")
            description.targets[parts[1]].push_back(field);
        else if (kind == "directory")
            description.directories[parts[1]].push_back(field);
        else if (kind == "variable")
            description.variables[parts[1]].push_back(field);
    }

    return description;
}

std::string findField(const RecordFields& fields, std::string_view name)
{
    for (const auto& [field, value]: fields)
    {
        if (field == name)
            return value;
    }

    return {};
}

std::optional<ConfiguredPackage>
    findOwningPackage(const std::vector<ConfiguredPackage>& packages,
                      const std::string& directory)
{
    auto owner = std::optional<ConfiguredPackage> {};

    for (const auto& package: packages)
    {
        auto isLongerMatch =
            !owner || package.sourceDirectory.size() > owner->sourceDirectory.size();

        if (isInsideDirectory(directory, package.sourceDirectory) && isLongerMatch)
            owner = package;
    }

    return owner;
}

std::set<std::string> collectLinkClosure(const ConfigureDescription& description,
                                         const std::string& target)
{
    auto closure = std::set<std::string> {};
    auto pending =
        splitCMakeList(findTargetField(description, target, "LINKED_TARGETS"));

    for (const auto& linked: splitCMakeList(
             findTargetField(description, target, "INTERFACE_LINKED_TARGETS")))
        pending.push_back(linked);

    while (!pending.empty())
    {
        auto next = pending.back();
        pending.pop_back();

        if (next == target || !closure.insert(next).second)
            continue;

        for (const auto& linked: splitCMakeList(
                 findTargetField(description, next, "INTERFACE_LINKED_TARGETS")))
            pending.push_back(linked);
    }

    return closure;
}

std::optional<std::string> findReasonNotToCache(const LibraryCacheInputs& inputs,
                                                const std::string& target)
{
    auto directory = findTargetField(inputs.description, target, "SOURCE_DIR");
    auto owner = findOwningPackage(inputs.packages, directory);

    if (!owner || owner->hash.empty())
    {
        return "its package is not a locked package from the cache: it comes from a "
               "local SOURCE_DIR, a <Name>_SOURCE override or a SOURCE_DIR checkout";
    }

    if (auto reason = findReasonInSources(inputs.description, target))
        return reason;

    if (auto reason = findReasonInDebugInformation(inputs.description, target))
    {
        return "MSVC writes its debug information to a separate .pdb, which the "
               "cache does not store: "
               + *reason
               + " (CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded keeps it in the "
                 "library)";
    }

    if (auto reason = findReasonInSplitDebugInformation(inputs.description, target))
        return *reason;

    if (auto linked = findLinkedTargetOutsideCachedPackages(inputs, target))
    {
        return "it links " + *linked
               + ", which is not from a locked package, so the key cannot cover its "
                 "headers";
    }

    return std::nullopt;
}

LibraryKey computeLibraryKey(const LibraryCacheInputs& inputs,
                             const std::string& target)
{
    auto text = KeyText {inputs.paths};
    text.addLine("format",
                 "lockstep-library-cache",
                 std::to_string(libraryCacheFormatVersion));

    if (!isCacheDirectoryMappedAway(inputs, target))
        text.addLineKeepingPaths(cacheDirectoryKeyLabel,
                                 cacheDirectoryKeyName,
                                 inputs.paths.cacheDirectory);

    auto filter = createKeyFieldFilter(inputs.description, target);
    auto ownFilter = filter;

    for (auto field: ownFieldsLeftOutOfKey)
        ownFilter.leaveOut(field);

    addTarget(text, inputs, target, ownFilter);

    auto directory = findTargetField(inputs.description, target, "SOURCE_DIR");
    text.addFields(
        "directory", findSubject(inputs.description.directories, directory), filter);

    for (const auto& linked: collectLinkClosure(inputs.description, target))
        addTarget(text, inputs, linked, filter);

    return {computeSha256Hex(text.getText()), text.getText()};
}

std::set<std::string> collectUpperCaseIdentifiers(std::string_view text)
{
    auto identifiers = std::set<std::string> {};
    auto isIdentifierCharacter = [](char character)
    {
        return std::isalnum(static_cast<unsigned char>(character))
               || character == '_';
    };

    for (auto start = std::size_t {0}; start < text.size();)
    {
        if (!isIdentifierCharacter(text[start]))
        {
            ++start;
            continue;
        }

        auto end = start;

        while (end < text.size() && isIdentifierCharacter(text[end]))
            ++end;

        auto word = text.substr(start, end - start);
        auto isUpperCase =
            std::isupper(static_cast<unsigned char>(word.front()))
            && std::ranges::none_of(
                word, [](char character) { return std::islower(character); });

        if (isUpperCase)
            identifiers.insert(std::string {word});

        start = end;
    }

    return identifiers;
}

std::set<std::string>
    collectPackageVariableNames(const eacp::FilePath& sourceDirectory)
{
    auto options = eacp::Files::DirectoryOptions {};
    options.recursive = true;
    options.pattern = "CMakeLists.txt;*.cmake";
    auto names = std::set<std::string> {};

    for (const auto& file: eacp::Files::listFiles(sourceDirectory, options))
        names.merge(collectUpperCaseIdentifiers(eacp::Files::readFile(file)));

    return names;
}

bool isCommandInput(const std::vector<std::string>& command,
                    const eacp::FilePath& library,
                    const eacp::FilePath& workingDirectory)
{
    auto libraryPath = eacp::toStdPath(library);
    auto directory = eacp::toStdPath(workingDirectory);
    auto isLibrary = [&](const std::string& argument)
    {
        return eacp::FilePath {argument}.fileName() == library.fileName()
               && isSameFile(directory / eacp::toStdPath(eacp::FilePath {argument}),
                             libraryPath);
    };

    for (const auto& argument: command)
    {
        if (isLibrary(argument))
            return true;

        if (!argument.starts_with('@'))
            continue;

        auto responseFile =
            eacp::FilePath {(directory / argument.substr(1)).generic_string()};

        if (std::ranges::any_of(readResponseFile(responseFile), isLibrary))
            return true;
    }

    return false;
}
} // namespace lockstep
