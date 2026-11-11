#include "LibraryCache.h"
#include "CompileFlags.h"
#include "KeyText.h"
#include "PackageArguments.h"
#include "Sha256.h"

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace lockstep
{
namespace
{
struct CachedTargetKindNames
{
    CachedTargetKind kind;
    std::string_view targetType;
    std::string_view name;
    std::string_view pluralName;
};

constexpr auto cachedTargetKindNames = std::array<CachedTargetKindNames, 3> {
    {{CachedTargetKind::staticLibrary,
      "STATIC_LIBRARY",
      "static library",
      "static libraries"},
     {CachedTargetKind::executable, "EXECUTABLE", "executable", "executables"},
     {CachedTargetKind::objectLibrary,
      "OBJECT_LIBRARY",
      "object library",
      "object libraries"}}};

const CachedTargetKindNames& findKindNames(CachedTargetKind kind)
{
    for (const auto& names: cachedTargetKindNames)
    {
        if (names.kind == kind)
            return names;
    }

    throw std::logic_error("the library cache has no name for this kind of entry");
}

CachedTargetKind getCachedTargetKind(const RecordFields& fields)
{
    if (auto kind = findCachedTargetKind(fields))
        return *kind;

    throw std::logic_error("the library cache does not store a target of type "
                           + findField(fields, "TYPE"));
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

    auto isStaticLibrary =
        findTargetField(description, target, "TYPE") == "STATIC_LIBRARY";

    if (isStaticLibrary && isUsedThroughTargetObjects(description, target))
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
// The command's arguments, with the words of each @<file> response file in its
// place.
std::vector<std::string> expandResponseFiles(const std::vector<std::string>& command,
                                             const eacp::FilePath& workingDirectory)
{
    auto directory = eacp::toStdPath(workingDirectory);
    auto expanded = std::vector<std::string> {};

    for (const auto& argument: command)
    {
        if (!argument.starts_with('@'))
        {
            expanded.push_back(argument);
            continue;
        }

        auto responseFile =
            eacp::FilePath {(directory / argument.substr(1)).generic_string()};

        for (const auto& word: readResponseFile(responseFile))
            expanded.push_back(word);
    }

    return expanded;
}

} // namespace

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

std::optional<CachedTargetKind> findCachedTargetKind(const RecordFields& fields)
{
    auto type = findField(fields, "TYPE");

    for (const auto& names: cachedTargetKindNames)
    {
        if (names.targetType == type)
            return names.kind;
    }

    return std::nullopt;
}

std::string getCachedTargetKindName(CachedTargetKind kind)
{
    return std::string {findKindNames(kind).name};
}

std::string getCachedTargetKindPluralName(CachedTargetKind kind)
{
    return std::string {findKindNames(kind).pluralName};
}

std::optional<std::string>
    findReasonNotToCacheCompiledCode(const LibraryCacheInputs& inputs,
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
                 "object files)";
    }

    return findReasonInSplitDebugInformation(inputs.description, target);
}

std::optional<std::string> findReasonNotToCache(const LibraryCacheInputs& inputs,
                                                const std::string& target)
{
    if (auto reason = findReasonNotToCacheCompiledCode(inputs, target))
        return reason;

    if (auto linked = findLinkedTargetOutsideCachedPackages(inputs, target))
    {
        return "it links " + *linked
               + ", which is not from a locked package, so the key cannot cover its "
                 "headers";
    }

    return std::nullopt;
}

void addFormat(KeyText& text, CachedTargetKind kind)
{
    text.addLine("format",
                 "lockstep-library-cache",
                 std::to_string(libraryCacheFormatVersion));
    text.addLine("kind", getCachedTargetKindName(kind), {});
}

LibraryKey computeLibraryKey(const LibraryCacheInputs& inputs,
                             const std::string& target)
{
    auto text = KeyText {inputs.paths};
    addFormat(text,
              getCachedTargetKind(findSubject(inputs.description.targets, target)));

    if (!isCacheDirectoryMappedAway(inputs, target))
        text.addCacheDirectory();

    auto filter = createKeyFieldFilter(inputs.description, target);
    addOwnTarget(text, inputs, target, filter);

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
                    const eacp::FilePath& file,
                    const eacp::FilePath& workingDirectory)
{
    auto filePath = eacp::toStdPath(file);
    auto directory = eacp::toStdPath(workingDirectory);
    auto isFile = [&](const std::string& argument)
    {
        return eacp::FilePath {argument}.fileName() == file.fileName()
               && isSameFile(directory / eacp::toStdPath(eacp::FilePath {argument}),
                             filePath);
    };

    return std::ranges::any_of(expandResponseFiles(command, workingDirectory),
                               isFile);
}
} // namespace lockstep
