#include "Git.h"

#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace lockstep
{
namespace
{
constexpr auto gitExecutable = "git";
constexpr auto fullShaLength = 40;

std::string joinArguments(const eacp::Vector<std::string>& arguments)
{
    auto joined = std::string {gitExecutable};

    for (const auto& argument: arguments)
        joined += ' ' + argument;

    return joined;
}

eacp::Vector<std::string>
    createMirrorArguments(const eacp::FilePath& mirror,
                          const eacp::Vector<std::string>& rest)
{
    auto arguments = eacp::Vector<std::string> {"--git-dir=" + mirror.str()};
    arguments.addFrom(rest);
    return arguments;
}

std::string runGitOrThrow(
    const eacp::Vector<std::string>& arguments,
    const eacp::Vector<eacp::Processes::EnvironmentVariable>& environment = {})
{
    auto output = runGit(arguments, environment);

    if (!output)
        throw std::runtime_error(joinArguments(arguments) + " failed");

    return *output;
}

bool isFullSha(const std::string& ref)
{
    return ref.size() == fullShaLength
           && std::all_of(
               ref.begin(), ref.end(), [](char c) { return std::isxdigit(c); });
}

bool isRemoteRefName(const eacp::FilePath& mirror, const std::string& ref)
{
    auto listed =
        runGitOrThrow(createMirrorArguments(mirror, {"ls-remote", "origin", ref}));
    return !listed.empty();
}

void fetchEveryBranchAndTag(const eacp::FilePath& mirror)
{
    auto arguments = createMirrorArguments(
        mirror,
        {"fetch", "--quiet", "--tags", "origin", "+refs/heads/*:refs/heads/*"});
    auto isShallow = runGitOrThrow(
        createMirrorArguments(mirror, {"rev-parse", "--is-shallow-repository"}));

    if (isShallow == "true")
        arguments.add("--unshallow");

    runGitOrThrow(arguments);
}
} // namespace

std::optional<std::string>
    runGit(const eacp::Vector<std::string>& arguments,
           const eacp::Vector<eacp::Processes::EnvironmentVariable>& environment)
{
    auto options = eacp::Processes::ProcessOptions {};
    options.executable = gitExecutable;
    options.arguments = arguments;
    options.environment = environment;
    options.environment.add({"GIT_TERMINAL_PROMPT", "0"});

    auto result = eacp::Processes::run(options);

    if (!result.launched)
    {
        eacp::Console::printError("lockstep: cannot run git; is it on PATH?");
        return std::nullopt;
    }

    if (result.exitCode != 0)
    {
        if (eacp::Strings::trim(result.errorOutput).empty())
            return std::nullopt;

        eacp::Console::printError("lockstep: " + joinArguments(arguments) + ":\n"
                                  + eacp::Strings::trim(result.errorOutput));
        return std::nullopt;
    }

    return eacp::Strings::trim(result.output);
}

void createBareMirror(const eacp::FilePath& mirror, const std::string& url)
{
    if (eacp::Files::isDirectory(mirror))
        return;

    runGitOrThrow({"init", "--quiet", "--bare", mirror.str()});
    runGitOrThrow(createMirrorArguments(mirror, {"remote", "add", "origin", url}));
}

bool hasCommit(const eacp::FilePath& mirror, const std::string& commit)
{
    auto arguments = createMirrorArguments(
        mirror, {"rev-parse", "--quiet", "--verify", commit + "^{commit}"});
    return runGit(arguments).has_value();
}

std::string fetchRefIntoMirror(const eacp::FilePath& mirror, const std::string& ref)
{
    if (isFullSha(ref) || isRemoteRefName(mirror, ref))
    {
        runGitOrThrow(createMirrorArguments(
            mirror,
            {"fetch", "--quiet", "--no-tags", "--depth", "1", "origin", ref}));
        return runGitOrThrow(
            createMirrorArguments(mirror, {"rev-parse", "FETCH_HEAD^{commit}"}));
    }

    fetchEveryBranchAndTag(mirror);
    return runGitOrThrow(
        createMirrorArguments(mirror, {"rev-parse", ref + "^{commit}"}));
}

void checkOutCommit(const eacp::FilePath& mirror,
                    const std::string& commit,
                    const eacp::FilePath& destination)
{
    auto indexFile = destination.sibling(destination.fileName() + ".index");
    auto arguments = createMirrorArguments(mirror,
                                           {"--work-tree=" + destination.str(),
                                            "-c",
                                            "core.autocrlf=false",
                                            "-c",
                                            "core.eol=lf",
                                            "-c",
                                            "core.symlinks=true",
                                            "checkout",
                                            "--quiet",
                                            "--force",
                                            commit,
                                            "--",
                                            "."});

    runGitOrThrow(arguments, {{"GIT_INDEX_FILE", indexFile.str()}});
    eacp::Files::removeFile(indexFile);
}

void applyPatch(const eacp::FilePath& tree, const eacp::FilePath& patch)
{
    runGitOrThrow({"-C", tree.str(), "apply", "--whitespace=nowarn", patch.str()});
}
} // namespace lockstep
