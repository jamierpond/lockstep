#include "KeyText.h"
#include "PackageArguments.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace lockstep
{
namespace
{
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

// The target settings that change only what a link writes, not object code:
// they stay out of a library's key and go into an executable's.
constexpr auto linkOnlyTargetFields =
    std::array<std::string_view, 25> {"LINK_OPTIONS",
                                      "LINK_FLAGS",
                                      "LINK_DIRECTORIES",
                                      "LINK_DEPENDS",
                                      "LINK_WHAT_YOU_USE",
                                      "LINKER_TYPE",
                                      "LINK_SEARCH_START_STATIC",
                                      "LINK_SEARCH_END_STATIC",
                                      "LINK_LIBRARY_OVERRIDE",
                                      "INTERFACE_LINK_OPTIONS",
                                      "INTERFACE_LINK_DIRECTORIES",
                                      "INTERFACE_LINK_DEPENDS",
                                      "BUILD_RPATH",
                                      "BUILD_RPATH_USE_ORIGIN",
                                      "SKIP_BUILD_RPATH",
                                      "BUILD_WITH_INSTALL_RPATH",
                                      "INSTALL_RPATH",
                                      "INSTALL_NAME_DIR",
                                      "MACOSX_RPATH",
                                      "MACOSX_BUNDLE",
                                      "WIN32_EXECUTABLE",
                                      "ENABLE_EXPORTS",
                                      "OUTPUT_NAME",
                                      "SUFFIX",
                                      "PREFIX"};

constexpr auto linkOnlyFieldPrefixes =
    std::array<std::string_view, 9> {"LINK_FLAGS_",
                                     "OUTPUT_NAME_",
                                     "IMPORTED_",
                                     "MAP_IMPORTED_CONFIG_",
                                     "LINKED_NAMES_NOT_VISIBLE",
                                     "CMAKE_EXE_LINKER_FLAGS",
                                     "CMAKE_LINKER",
                                     "CMAKE_MT",
                                     "CMAKE_EXECUTABLE_SUFFIX"};

constexpr auto linkOnlyDirectoryFields =
    std::array<std::string_view, 1> {"CMAKE_SKIP_RPATH"};

// The CMAKE_<LANG>_<setting> names that describe the link, not the compile.
constexpr auto linkOnlyLanguageSettings =
    std::array<std::string_view, 8> {"COMPILER_LINKER",
                                     "LINK_EXECUTABLE",
                                     "STANDARD_LIBRARIES",
                                     "IMPLICIT_LINK_",
                                     "LINK_PIE_SUPPORTED",
                                     "LINK_NO_PIE_SUPPORTED",
                                     "LINK_OPTIONS_PIE",
                                     "LINK_OPTIONS_NO_PIE"};

constexpr auto linkerLauncherSuffix = std::string_view {"_LINKER_LAUNCHER"};

constexpr auto cacheDirectoryKeyLabel = std::string_view {"machine"};
constexpr auto cacheDirectoryKeyName = "cache-directory";

// CMake sets these in every function call, and a package's directory inherits
// them from the lockstep_add_package (or FetchContent_MakeAvailable) call that
// added it, so their values are the text of that call. ARGV0, ARGV1, ... too.
constexpr auto functionArgumentVariables =
    std::array<std::string_view, 3> {"ARGC", "ARGN", "ARGV"};
constexpr auto numberedFunctionArgumentPrefix = std::string_view {"ARGV"};

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

void leaveOutLinkSettings(KeyFieldFilter& filter,
                          const ConfigureDescription& description)
{
    for (auto field: linkOnlyTargetFields)
        filter.leaveOut(field);

    for (auto field: linkOnlyDirectoryFields)
        filter.leaveOut(field);

    for (auto prefix: linkOnlyFieldPrefixes)
        filter.leaveOutPrefix(std::string {prefix});

    filter.leaveOutSuffix(std::string {linkerLauncherSuffix});

    for (const auto& language:
         splitCMakeList(findField(description.globals, "ENABLED_LANGUAGES")))
    {
        for (auto setting: linkOnlyLanguageSettings)
            filter.leaveOutPrefix("CMAKE_" + language + "_" + std::string {setting});
    }
}

// The fields every key leaves out, whatever the target writes.
KeyFieldFilter createSharedKeyFieldFilter(const ConfigureDescription& description,
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

    return filter;
}
} // namespace

bool KeyFieldFilter::isLeftOut(const std::string& field) const
{
    auto isPrefixOfField = [&](const std::string& prefix)
    { return field.starts_with(prefix); };

    auto isSuffixOfField = [&](const std::string& suffix)
    { return field.ends_with(suffix); };

    return std::ranges::find(leftOutNames, field) != leftOutNames.end()
           || std::ranges::any_of(leftOutPrefixes, isPrefixOfField)
           || std::ranges::any_of(leftOutSuffixes, isSuffixOfField);
}

KeyText::KeyText(const MachinePaths& pathsToUse)
    : paths(pathsToUse)
{
}

void KeyText::addLine(std::string_view label,
                      const std::string& name,
                      const std::string& value)
{
    auto independent = makeMachineIndependent(value, paths);
    text += std::string {label} + " " + name + "="
            + replaceAll(independent, "\n", "\\n") + "\n";
}

void KeyText::addCacheDirectory()
{
    text += std::string {cacheDirectoryKeyLabel} + " " + cacheDirectoryKeyName + "="
            + paths.cacheDirectory + "\n";
}

void KeyText::addFields(std::string_view label,
                        const RecordFields& fields,
                        const KeyFieldFilter& filter)
{
    for (const auto& [field, value]: fields)
    {
        if (!filter.isLeftOut(field))
            addLine(label, field, value);
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

void addOwnTarget(KeyText& text,
                  const LibraryCacheInputs& inputs,
                  const std::string& target,
                  const KeyFieldFilter& filter)
{
    auto ownFilter = filter;

    for (auto field: ownFieldsLeftOutOfKey)
        ownFilter.leaveOut(field);

    addTarget(text, inputs, target, ownFilter);
}

KeyFieldFilter createKeyFieldFilter(const ConfigureDescription& description,
                                    const std::string& target)
{
    auto filter = createSharedKeyFieldFilter(description, target);
    leaveOutLanguagesNotCompiled(filter, description, target);
    leaveOutLinkSettings(filter, description);
    return filter;
}

KeyFieldFilter createLinkSettingFilter(const ConfigureDescription& description)
{
    auto filter = KeyFieldFilter {};
    leaveOutLinkSettings(filter, description);
    return filter;
}

KeyFieldFilter createLinkKeyFieldFilter(const ConfigureDescription& description,
                                        const std::string& target)
{
    return createSharedKeyFieldFilter(description, target);
}
} // namespace lockstep
