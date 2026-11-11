#include "CompilerExecutable.h"
#include "RememberedFileHash.h"
#include "Sha256.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/File.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/Strings.h>

#include <emberstore/Database.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <set>

namespace lockstep
{
namespace
{
constexpr auto rememberedHashesName = "compiler-executables";
constexpr auto scriptStart = std::string_view {"#!"};
constexpr auto installedDirectoryLabel = std::string_view {"InstalledDir:"};

constexpr auto clangCompilerIds =
    std::array<std::string_view, 3> {"Clang", "AppleClang", "IntelLLVM"};

struct GnuCompilerProgram
{
    std::string_view language;
    std::string_view program;
};

constexpr auto gnuCompilerPrograms =
    std::array<GnuCompilerProgram, 4> {{{"C", "cc1"},
                                        {"CXX", "cc1plus"},
                                        {"OBJC", "cc1obj"},
                                        {"OBJCXX", "cc1objplus"}}};

// MSVC's driver, and its front ends and back end beside it.
constexpr auto msvcCompilerFiles =
    std::array<std::string_view, 4> {"cl.exe", "c1.dll", "c1xx.dll", "c2.dll"};

constexpr auto clangStem = std::string_view {"clang"};

bool isSameFileAsBefore(const RememberedFileHash& known,
                        const RememberedFileHash& now)
{
    auto isSameStamp =
        known.size == now.size && known.modificationTime == now.modificationTime;
    return known.path == now.path && isSameStamp;
}

std::optional<eacp::FilePath> findRegularFile(const eacp::FilePath& path)
{
    auto error = std::error_code {};
    auto resolved = std::filesystem::canonical(eacp::toStdPath(path), error);

    if (error || !std::filesystem::is_regular_file(resolved, error))
        return std::nullopt;

    return eacp::FilePath {resolved.generic_string()};
}

bool isScript(const eacp::FilePath& file)
{
    auto reader = eacp::File {file};
    auto start = std::array<std::uint8_t, scriptStart.size()> {};
    auto bytesRead = reader.read(0, {start.data(), static_cast<int>(start.size())});
    auto text = std::string_view {reinterpret_cast<const char*>(start.data()),
                                  static_cast<std::size_t>(std::max(bytesRead, 0))};
    return text == scriptStart;
}

std::string runCompiler(const std::string& compiler, const std::string& argument)
{
    auto options = eacp::Processes::ProcessOptions {};
    options.executable = compiler;
    options.arguments = {argument};
    options.mergeErrorOutput = true;
    options.workingDirectory = eacp::FilePath::tempDirectory().str();
    auto result = eacp::Processes::run(options);

    if (!result.launched || result.exitCode != 0)
        return {};

    return result.output;
}

std::optional<eacp::FilePath> findClangExecutable(const eacp::FilePath& compiler)
{
    for (const auto& line:
         eacp::Strings::split(runCompiler(compiler.str(), "--version"), '\n'))
    {
        auto trimmed = eacp::Strings::trim(line);

        if (!trimmed.starts_with(installedDirectoryLabel))
            continue;

        auto directory = eacp::FilePath {
            eacp::Strings::trim(trimmed.substr(installedDirectoryLabel.size()))};

        if (auto sameName = findRegularFile(directory / compiler.fileName()))
            return sameName;

        return findRegularFile(directory
                               / (std::string {clangStem} + compiler.extension()));
    }

    return std::nullopt;
}

std::optional<eacp::FilePath>
    findGnuCompilerProper(const ConfiguredCompiler& compiler)
{
    auto isLanguage = [&](const GnuCompilerProgram& program)
    { return program.language == compiler.language; };
    auto found = std::ranges::find_if(gnuCompilerPrograms, isLanguage);

    if (found == gnuCompilerPrograms.end())
        return std::nullopt;

    auto output = runCompiler(compiler.path,
                              "-print-prog-name=" + std::string {found->program});
    auto path = eacp::FilePath {eacp::Strings::trim(output)};

    if (!path.isAbsolute())
        return std::nullopt;

    return findRegularFile(path);
}

std::vector<eacp::FilePath> findMsvcCompilerFiles(const eacp::FilePath& compiler)
{
    auto files = std::vector<eacp::FilePath> {};

    for (auto name: msvcCompilerFiles)
    {
        if (auto file =
                findRegularFile(compiler.parentDirectory() / std::string {name}))
            files.push_back(*file);
    }

    return files;
}

bool isClang(const std::string& compilerId)
{
    return std::ranges::find(clangCompilerIds, compilerId) != clangCompilerIds.end();
}

// The compiler's own files, after any wrapper script.
std::vector<eacp::FilePath>
    findCompilerProperFiles(const ConfiguredCompiler& compiler,
                            const eacp::FilePath& configured)
{
    if (compiler.compilerId == "MSVC")
    {
        if (auto files = findMsvcCompilerFiles(configured); !files.empty())
            return files;
    }

    auto proper = std::optional<eacp::FilePath> {};

    if (isClang(compiler.compilerId))
        proper = findClangExecutable(eacp::FilePath {compiler.path});
    else if (compiler.compilerId == "GNU")
        proper = findGnuCompilerProper(compiler);

    if (proper)
        return {*proper};

    return {configured};
}

class FileHasher
{
public:
    explicit FileHasher(const eacp::FilePath& directory)
        : document(emberstore::Database {directory}.document<RememberedFileHashes>(
              rememberedHashesName))
    {
        remembered = document.read();
    }

    std::string hash(const eacp::FilePath& file)
    {
        auto reader = eacp::File {file};
        auto entry = RememberedFileHash {
            file.str(), reader.size(), reader.modificationTime(), {}};

        for (const auto& known: remembered)
        {
            if (isSameFileAsBefore(known, entry))
                return known.sha256;
        }

        entry.sha256 = computeFileSha256Hex(file);
        added.push_back(entry);
        return entry.sha256;
    }

    // A lost write costs only a rehash on the next configure.
    void remember()
    {
        if (added.empty())
            return;

        auto addEntries = [&](RememberedFileHashes& hashes)
        {
            for (const auto& entry: added)
            {
                auto isSamePath = [&](const RememberedFileHash& known)
                { return known.path == entry.path; };
                std::erase_if(hashes, isSamePath);
                hashes.push_back(entry);
            }
        };

        document.mutate(addEntries);
    }

private:
    emberstore::Document<RememberedFileHashes> document;
    RememberedFileHashes remembered;
    std::vector<RememberedFileHash> added;
};

std::string hashFiles(FileHasher& hasher, const std::vector<eacp::FilePath>& files)
{
    if (files.size() == 1)
        return hasher.hash(files.front());

    auto lines = std::string {};

    for (const auto& file: files)
        lines += hasher.hash(file) + "\n";

    if (lines.empty())
        return {};

    return computeSha256Hex(lines);
}

std::set<ConfiguredCompiler>
    collectConfiguredCompilers(const ConfigureDescription& description)
{
    auto compilers = std::set<ConfiguredCompiler> {};
    auto languages =
        splitCMakeList(findField(description.globals, "ENABLED_LANGUAGES"));

    for (const auto& [directory, fields]: description.directories)
    {
        for (const auto& language: languages)
        {
            auto compiler = findConfiguredCompiler(fields, language);

            if (!compiler.path.empty())
                compilers.insert(compiler);
        }
    }

    return compilers;
}
} // namespace

std::vector<eacp::FilePath>
    findCompilerExecutableFiles(const ConfiguredCompiler& compiler)
{
    auto configured = findRegularFile(eacp::FilePath {compiler.path});

    if (!configured)
        return {};

    auto files = std::vector<eacp::FilePath> {};

    if (isScript(*configured))
        files.push_back(*configured);

    for (const auto& file: findCompilerProperFiles(compiler, *configured))
    {
        if (std::ranges::find(files, file) == files.end())
            files.push_back(file);
    }

    return files;
}

CompilerExecutableHashes
    hashCompilerExecutables(const ConfigureDescription& description,
                            const eacp::FilePath& rememberedHashesDirectory)
{
    auto hasher = FileHasher {rememberedHashesDirectory};
    auto hashes = CompilerExecutableHashes {};

    for (const auto& compiler: collectConfiguredCompilers(description))
        hashes[compiler] = hashFiles(hasher, findCompilerExecutableFiles(compiler));

    hasher.remember();
    return hashes;
}

ConfiguredCompiler findConfiguredCompiler(const RecordFields& directoryFields,
                                          const std::string& language)
{
    auto prefix = "CMAKE_" + language + "_COMPILER";
    return {language,
            findField(directoryFields, prefix + "_ID"),
            findField(directoryFields, prefix)};
}
} // namespace lockstep
