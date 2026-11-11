#include "ExecutableCache.h"
#include "CompileFlags.h"
#include "LibraryCache.h"
#include "PackageArguments.h"
#include "Sha256.h"

#include <algorithm>
#include <array>

namespace lockstep
{
namespace
{
// MSVC's linker writes a .pdb beside the executable for these, which the cache
// does not store.
constexpr auto linkerDebugInformationFlags =
    std::array<std::string_view, 2> {"/DEBUG", "-DEBUG"};
constexpr auto linkerNoDebugInformationFlags =
    std::array<std::string_view, 2> {"/DEBUG:NONE", "-DEBUG:NONE"};

// The CMake module's fields of names in a target's link libraries that hold
// "::", so name targets, but that the top-level directory cannot see.
constexpr auto linkedNamesNotVisiblePrefix =
    std::string_view {"LINKED_NAMES_NOT_VISIBLE"};

constexpr auto linkLibraryFields =
    std::array<std::string_view, 2> {"LINK_LIBRARIES", "INTERFACE_LINK_LIBRARIES"};

// What can follow a directory in a setting when the setting names a path in it.
constexpr auto pathContinuations = std::string_view {"/ \t\n;\"'>,"};

// CMake 3.29 and newer read the linker's version into this setting.
constexpr auto linkerVersionSetting = std::string_view {"COMPILER_LINKER_VERSION"};

const RecordFields& findDirectoryFields(const ConfigureDescription& description,
                                        const std::string& target)
{
    auto directory = findTargetField(description, target, "SOURCE_DIR");
    return findSubject(description.directories, directory);
}

std::string getUpperCaseBuildType(const RecordFields& directoryFields)
{
    return toUpperCase(findField(directoryFields, "CMAKE_BUILD_TYPE"));
}

// The text of every setting that puts flags on the executable's link line.
std::vector<std::string>
    collectLinkFlagTexts(const ConfigureDescription& description,
                         const std::string& target)
{
    const auto& directoryFields = findDirectoryFields(description, target);
    auto configuration = getUpperCaseBuildType(directoryFields);
    auto texts = std::vector<std::string> {
        findTargetField(description, target, "LINK_OPTIONS"),
        findTargetField(description, target, "LINK_FLAGS"),
        findTargetField(description, target, "LINK_FLAGS_" + configuration),
        findField(directoryFields, "LINK_OPTIONS"),
        findField(directoryFields, "CMAKE_EXE_LINKER_FLAGS"),
        findField(directoryFields, "CMAKE_EXE_LINKER_FLAGS_" + configuration)};

    for (const auto& linked: collectLinkClosure(description, target))
        texts.push_back(
            findTargetField(description, linked, "INTERFACE_LINK_OPTIONS"));

    return texts;
}

bool asksLinkerForProgramDatabase(const std::string& text)
{
    auto flags = toUpperCase(text);

    for (auto flag: linkerNoDebugInformationFlags)
        flags = replaceAll(flags, flag, {});

    auto isInFlags = [&](std::string_view flag)
    { return containsFlag(flags, flag); };
    return std::ranges::any_of(linkerDebugInformationFlags, isInFlags);
}

// The languages compiled into the executable: its own and those of every
// library it links.
std::vector<std::string>
    collectLinkedLanguages(const ConfigureDescription& description,
                           const std::string& target)
{
    auto languages =
        splitCMakeList(findTargetField(description, target, "COMPILED_LANGUAGES"));

    for (const auto& linked: collectLinkClosure(description, target))
    {
        for (const auto& language: splitCMakeList(
                 findTargetField(description, linked, "COMPILED_LANGUAGES")))
            languages.push_back(language);
    }

    std::ranges::sort(languages);
    auto duplicates = std::ranges::unique(languages);
    languages.erase(duplicates.begin(), duplicates.end());
    return languages;
}

std::optional<std::string>
    findReasonInLinkerDebugInformation(const ConfigureDescription& description,
                                       const std::string& target)
{
    const auto& directoryFields = findDirectoryFields(description, target);

    if (!compilesForMsvcAbi(directoryFields,
                            collectLinkedLanguages(description, target)))
        return std::nullopt;

    for (const auto& text: collectLinkFlagTexts(description, target))
    {
        if (asksLinkerForProgramDatabase(text))
            return "its link flags hold /DEBUG, so MSVC's linker writes its debug "
                   "information to a separate .pdb, which the cache does not store";
    }

    return std::nullopt;
}

std::optional<std::string>
    findReasonInOutput(const ConfigureDescription& description,
                       const std::string& target)
{
    if (isCMakeTrue(findTargetField(description, target, "MACOSX_BUNDLE")))
        return "it is an app bundle, a directory the cache does not store";

    if (isCMakeTrue(findTargetField(description, target, "ENABLE_EXPORTS")))
        return "it exports symbols, so its link writes an import library beside it, "
               "which the cache does not store";

    return std::nullopt;
}

std::vector<std::string>
    collectSelfAndLinkClosure(const ConfigureDescription& description,
                              const std::string& target)
{
    auto targets = std::vector<std::string> {target};

    for (const auto& linked: collectLinkClosure(description, target))
        targets.push_back(linked);

    return targets;
}

std::optional<std::string>
    findReasonInLinkedNamesNotVisible(const ConfigureDescription& description,
                                      const std::string& target)
{
    for (const auto& linking: collectSelfAndLinkClosure(description, target))
    {
        for (const auto& [field, names]: findSubject(description.targets, linking))
        {
            if (field.starts_with(linkedNamesNotVisiblePrefix) && !names.empty())
                return "it links " + names
                       + ", a target a package's directory declares without GLOBAL, "
                         "so the key cannot cover what it links";
        }
    }

    return std::nullopt;
}

std::optional<std::string>
    findReasonInLinkedTarget(const LibraryCacheInputs& inputs,
                             const std::string& linked,
                             const LinkedLibraryKeys& libraryKeys)
{
    const auto& fields = findSubject(inputs.description.targets, linked);

    if (isCMakeTrue(findField(fields, "IMPORTED")))
        return std::nullopt;

    auto type = findField(fields, "TYPE");

    if (type == "SHARED_LIBRARY" || type == "MODULE_LIBRARY")
        return "it links " + linked
               + ", a shared library this build directory builds, which the cache "
                 "does not store";

    if (type == "EXECUTABLE")
        return "it links the executable " + linked;

    auto isLibrary = findCachedTargetKind(fields).has_value();

    if (isLibrary && !libraryKeys.contains(linked))
        return "it links " + linked + ", which the cache skips";

    return std::nullopt;
}

std::optional<std::string>
    findReasonInLinkedTargets(const LibraryCacheInputs& inputs,
                              const std::string& target,
                              const LinkedLibraryKeys& libraryKeys)
{
    if (auto linked = findLinkedTargetOutsideCachedPackages(inputs, target))
        return "it links " + *linked
               + ", which is not from a locked package, so the key cannot cover it";

    for (const auto& linked: collectLinkClosure(inputs.description, target))
    {
        if (auto reason = findReasonInLinkedTarget(inputs, linked, libraryKeys))
            return reason;
    }

    return std::nullopt;
}

bool namesDirectory(const std::string& text, const std::string& directory)
{
    auto forwardSlashText = toForwardSlashes(text);

    for (auto position = forwardSlashText.find(directory);
         position != std::string::npos;
         position = forwardSlashText.find(directory, position + 1))
    {
        auto end = position + directory.size();

        if (end == forwardSlashText.size()
            || pathContinuations.find(forwardSlashText[end])
                   != std::string_view::npos)
            return true;
    }

    return false;
}

bool isLinkSetting(const KeyFieldFilter& linkSettings, const std::string& field)
{
    return linkSettings.isLeftOut(field)
           || std::ranges::find(linkLibraryFields, field) != linkLibraryFields.end();
}

std::optional<std::string>
    findLinkSettingNamingDirectory(const RecordFields& fields,
                                   const KeyFieldFilter& linkSettings,
                                   const std::string& directory)
{
    for (const auto& [field, value]: fields)
    {
        if (isLinkSetting(linkSettings, field) && namesDirectory(value, directory))
            return field;
    }

    return std::nullopt;
}

// The first link setting, as "<field> of <subject>", that names a path in the
// build directory.
std::optional<std::string>
    findLinkSettingNamingBuildDirectory(const LibraryCacheInputs& inputs,
                                        const std::string& target)
{
    const auto& description = inputs.description;
    const auto& buildDirectory = inputs.paths.buildDirectory;
    auto linkSettings = createLinkSettingFilter(description);

    for (const auto& linking: collectSelfAndLinkClosure(description, target))
    {
        if (auto field = findLinkSettingNamingDirectory(
                findSubject(description.targets, linking),
                linkSettings,
                buildDirectory))
            return *field + " of " + linking;
    }

    if (auto field = findLinkSettingNamingDirectory(
            findDirectoryFields(description, target), linkSettings, buildDirectory))
        return *field + " of its directory";

    return std::nullopt;
}

std::optional<std::string>
    findReasonInLinker(const ConfigureDescription& description,
                       const std::string& target)
{
    const auto& directoryFields = findDirectoryFields(description, target);

    for (const auto& language: collectLinkedLanguages(description, target))
    {
        auto setting =
            "CMAKE_" + language + "_" + std::string {linkerVersionSetting};

        if (findField(directoryFields, setting).empty())
            return "CMake reported no version for its linker (" + setting
                   + ", which CMake 3.29 and newer set), so the key cannot cover "
                     "which linker links it";
    }

    return std::nullopt;
}
} // namespace

std::optional<std::string>
    findReasonNotToCacheExecutable(const LibraryCacheInputs& inputs,
                                   const std::string& target,
                                   const LinkedLibraryKeys& libraryKeys)
{
    const auto& description = inputs.description;

    if (auto reason = findReasonNotToCacheCompiledCode(inputs, target))
        return reason;

    if (auto reason = findReasonInOutput(description, target))
        return reason;

    if (auto reason = findReasonInLinkerDebugInformation(description, target))
        return reason;

    if (auto reason = findReasonInLinkedNamesNotVisible(description, target))
        return reason;

    if (auto reason = findReasonInLinkedTargets(inputs, target, libraryKeys))
        return reason;

    if (auto setting = findLinkSettingNamingBuildDirectory(inputs, target))
        return "its link settings name the build directory (" + *setting
               + "), so the executable could point into the build directory that "
                 "linked it";

    return findReasonInLinker(description, target);
}

LibraryKey computeExecutableKey(const LibraryCacheInputs& inputs,
                                const std::string& target,
                                const LinkedLibraryKeys& libraryKeys)
{
    auto text = KeyText {inputs.paths};
    addFormat(text, CachedTargetKind::executable);
    text.addCacheDirectory();

    auto filter = createLinkKeyFieldFilter(inputs.description, target);
    addOwnTarget(text, inputs, target, filter);
    text.addFields(
        "directory", findDirectoryFields(inputs.description, target), filter);

    for (const auto& linked: collectLinkClosure(inputs.description, target))
    {
        addTarget(text, inputs, linked, filter);

        if (auto key = libraryKeys.find(linked); key != libraryKeys.end())
            text.addLine("linked-library-key", linked, key->second);
    }

    return {computeSha256Hex(text.getText()), text.getText()};
}
} // namespace lockstep
