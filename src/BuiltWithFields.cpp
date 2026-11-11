#include "BuiltWithFields.h"
#include "CompilerExecutable.h"
#include "PackageArguments.h"
#include "Sha256.h"

#include <array>

namespace lockstep
{
namespace
{
constexpr auto compilerVersionSuffix = std::string_view {"_COMPILER_VERSION"};
constexpr auto linkerVersionSuffix = std::string_view {"_COMPILER_LINKER_VERSION"};

// The record field each directory setting moves into.
struct SettingInRecord
{
    std::string_view setting;
    std::string BuiltWith::* field;
};

constexpr auto settingsInRecord = std::array<SettingInRecord, 3> {
    {{"APPLE_SDK_VERSION", &BuiltWith::sdkVersion},
     {"APPLE_SDK_BUILD_VERSION", &BuiltWith::sdkBuildVersion},
     {"CMAKE_VERSION", &BuiltWith::cmakeVersion}}};

// Cross-compiling, CMAKE_SYSTEM_VERSION is the target's, such as an Android
// API level, which changes the code; only the host's own version moves out.
constexpr auto systemVersionSetting = std::string_view {"CMAKE_SYSTEM_VERSION"};

std::string getLanguageSetting(const std::string& language, std::string_view suffix)
{
    return "CMAKE_" + language + std::string {suffix};
}

// Moves the setting out of the key when the key would hold it, and returns
// whether it did.
bool moveSetting(KeyFieldFilter& filter,
                 const RecordFields& directoryFields,
                 const std::string& setting,
                 std::string& destination)
{
    if (filter.isLeftOut(setting))
        return false;

    destination = findField(directoryFields, setting);
    filter.leaveOut(setting);
    return true;
}

void moveCompilers(BuiltWith& builtWith,
                   const LibraryCacheInputs& inputs,
                   const RecordFields& directoryFields,
                   KeyFieldFilter& filter)
{
    auto languages =
        splitCMakeList(findField(inputs.description.globals, "ENABLED_LANGUAGES"));

    for (const auto& language: languages)
    {
        auto compiler = CompilerBuild {language, {}, {}, {}};

        if (!moveSetting(filter,
                         directoryFields,
                         getLanguageSetting(language, compilerVersionSuffix),
                         compiler.version))
            continue;

        moveSetting(filter,
                    directoryFields,
                    getLanguageSetting(language, linkerVersionSuffix),
                    compiler.linkerVersion);

        auto hash = inputs.compilerExecutableHashes.find(
            findConfiguredCompiler(directoryFields, language));

        if (hash != inputs.compilerExecutableHashes.end())
            compiler.executableSha256 = hash->second;

        builtWith.compilers.push_back(compiler);
    }

    std::ranges::sort(builtWith.compilers, {}, &CompilerBuild::language);
}
} // namespace

BuiltWith moveBuiltWithFieldsOutOfKey(const LibraryCacheInputs& inputs,
                                      const std::string& target,
                                      KeyFieldFilter& filter)
{
    auto directory = findTargetField(inputs.description, target, "SOURCE_DIR");
    const auto& directoryFields =
        findSubject(inputs.description.directories, directory);
    auto builtWith = BuiltWith {};

    moveCompilers(builtWith, inputs, directoryFields, filter);

    for (const auto& [setting, field]: settingsInRecord)
        moveSetting(
            filter, directoryFields, std::string {setting}, builtWith.*field);

    if (!isCMakeTrue(findField(directoryFields, "CMAKE_CROSSCOMPILING")))
        moveSetting(filter,
                    directoryFields,
                    std::string {systemVersionSetting},
                    builtWith.hostSystemVersion);

    return builtWith;
}
LibraryKey createLibraryKey(const std::string& inputs, const BuiltWith& builtWith)
{
    return {computeSha256Hex(inputs), inputs, builtWith, computeBuildId(builtWith)};
}
} // namespace lockstep
