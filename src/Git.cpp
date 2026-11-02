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
bool isRelativeUrl(const std::string& url)
{
    return url.starts_with("./") || url.starts_with("../");
}

// git's rule: each leading ../ drops one path component of the parent's URL.
std::string resolveRelativeUrl(std::string parentUrl, std::string_view relative)
{
    while (parentUrl.ends_with('/'))
        parentUrl.pop_back();

    while (true)
    {
        if (relative.starts_with("./"))
        {
            relative.remove_prefix(2);
        }
        else if (relative.starts_with("../"))
        {
            relative.remove_prefix(3);
            auto slash = parentUrl.find_last_of("/:");

            if (slash == std::string::npos)
                throw std::runtime_error("cannot resolve submodule URL ../ against "
                                         + parentUrl);

            parentUrl.erase(slash);
        }
        else
        {
            break;
        }
    }

    return parentUrl + "/" + std::string {relative};
}

std::string readSubmoduleSetting(const eacp::FilePath& gitmodules,
                                 const std::string& key)
{
    return runGitOrThrow({"config", "--file", gitmodules.str(), "--get", key});
}

std::string readGitlinkCommit(const eacp::FilePath& mirror,
                              const std::string& commit,
                              const std::string& path)
{
    auto entry = runGitOrThrow(
        createMirrorArguments(mirror, {"ls-tree", commit, "--", path}));
    auto modeTypeAndObject = eacp::Strings::split(entry, '\t', 2)[0];
    auto fields = eacp::Strings::split(modeTypeAndObject, ' ');

    if (fields.size() != 3 || fields[1] != "commit")
        throw std::runtime_error(".gitmodules names " + path
                                 + ", but the tree has no submodule there");

    return fields[2];
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

eacp::Vector<Submodule> listSubmodules(const eacp::FilePath& mirror,
                                       const std::string& commit,
                                       const eacp::FilePath& tree,
                                       const std::string& parentUrl)
{
    auto gitmodules = tree / ".gitmodules";
    auto submodules = eacp::Vector<Submodule> {};

    if (!eacp::Files::isFile(gitmodules))
        return submodules;

    auto pathKeys = runGit({"config",
                            "--file",
                            gitmodules.str(),
                            "--name-only",
                            "--get-regexp",
                            "^submodule\\..*\\.path$"});

    if (!pathKeys)
        return submodules;

    for (const auto& pathKey: eacp::Strings::split(*pathKeys, '\n'))
    {
        auto key = std::string {pathKey};
        auto prefix = key.substr(0, key.size() - std::string_view {".path"}.size());
        auto path = readSubmoduleSetting(gitmodules, key);
        auto url = readSubmoduleSetting(gitmodules, prefix + ".url");

        if (isRelativeUrl(url))
            url = resolveRelativeUrl(parentUrl, url);

        submodules.add({path, url, readGitlinkCommit(mirror, commit, path)});
    }

    return submodules;
}

void applyPatch(const eacp::FilePath& tree, const eacp::FilePath& patch)
{
    runGitOrThrow({"-C", tree.str(), "apply", "--whitespace=nowarn", patch.str()});
}
} // namespace lockstep
