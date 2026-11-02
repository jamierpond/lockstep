#include "Git.h"

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

#include <stdexcept>

namespace lockstep
{
namespace
{
constexpr auto gitExecutable = "git";

eacp::Processes::ProcessResult
    launchGit(const eacp::Vector<std::string>& arguments,
              const eacp::Vector<eacp::Processes::EnvironmentVariable>& environment)
{
    auto options = eacp::Processes::ProcessOptions {};
    options.executable = gitExecutable;
    options.arguments = arguments;
    options.environment = environment;
    options.environment.add({"GIT_TERMINAL_PROMPT", "0"});
    return eacp::Processes::run(options);
}

std::string joinArguments(const eacp::Vector<std::string>& arguments)
{
    auto joined = std::string {gitExecutable};

    for (const auto& argument: arguments)
        joined += ' ' + argument;

    return joined;
}

eacp::Vector<std::string> getMirrorArguments(const eacp::FilePath& mirror)
{
    return {"--git-dir=" + mirror.str()};
}

eacp::Vector<std::string> combineArguments(eacp::Vector<std::string> first,
                                           const eacp::Vector<std::string>& second)
{
    first.addFrom(second);
    return first;
}
} // namespace

std::string
    runGit(const eacp::Vector<std::string>& arguments,
           const eacp::Vector<eacp::Processes::EnvironmentVariable>& environment)
{
    auto result = launchGit(arguments, environment);

    if (!result.launched)
        throw std::runtime_error("cannot run git; is it on PATH?");

    if (result.exitCode != 0)
    {
        throw std::runtime_error(joinArguments(arguments) + " failed:\n"
                                 + eacp::Strings::trim(result.errorOutput));
    }

    return eacp::Strings::trim(result.output);
}

std::optional<std::string> tryRunGit(const eacp::Vector<std::string>& arguments)
{
    auto result = launchGit(arguments, {});

    if (!result.launched || result.exitCode != 0)
        return std::nullopt;

    return eacp::Strings::trim(result.output);
}

void createBareMirror(const eacp::FilePath& mirror, const std::string& url)
{
    if (eacp::Files::isDirectory(mirror))
        return;

    runGit({"init", "--quiet", "--bare", mirror.str()});
    runGit(combineArguments(getMirrorArguments(mirror),
                            {"remote", "add", "origin", url}));
}

bool hasCommit(const eacp::FilePath& mirror, const std::string& commit)
{
    auto arguments = combineArguments(getMirrorArguments(mirror),
                                      {"cat-file", "-e", commit + "^{commit}"});
    return tryRunGit(arguments).has_value();
}

std::string fetchRefIntoMirror(const eacp::FilePath& mirror, const std::string& ref)
{
    auto mirrorArguments = getMirrorArguments(mirror);
    auto shallowFetch = combineArguments(
        mirrorArguments,
        {"fetch", "--quiet", "--no-tags", "--depth", "1", "origin", ref});

    if (tryRunGit(shallowFetch))
        return runGit(
            combineArguments(mirrorArguments, {"rev-parse", "FETCH_HEAD^{commit}"}));

    auto fullFetch = combineArguments(
        mirrorArguments,
        {"fetch", "--quiet", "--tags", "origin", "+refs/heads/*:refs/heads/*"});
    auto isShallow = runGit(
        combineArguments(mirrorArguments, {"rev-parse", "--is-shallow-repository"}));

    if (isShallow == "true")
        fullFetch.add("--unshallow");

    runGit(fullFetch);
    return runGit(
        combineArguments(mirrorArguments, {"rev-parse", ref + "^{commit}"}));
}

void checkOutCommit(const eacp::FilePath& mirror,
                    const std::string& commit,
                    const eacp::FilePath& destination)
{
    auto indexFile = destination.sibling(destination.fileName() + ".index");
    auto arguments = combineArguments(getMirrorArguments(mirror),
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

    runGit(arguments, {{"GIT_INDEX_FILE", indexFile.str()}});
    eacp::Files::removeFile(indexFile);
}

void applyPatch(const eacp::FilePath& tree, const eacp::FilePath& patch)
{
    runGit({"-C", tree.str(), "apply", "--whitespace=nowarn", patch.str()});
}
} // namespace lockstep
