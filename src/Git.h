#pragma once

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <string>

namespace lockstep
{
// git's trimmed stdout, or nullopt when it cannot be launched or exits
// non-zero, in which case its stderr has been printed.
std::optional<std::string> runGit(
    const eacp::Vector<std::string>& arguments,
    const eacp::Vector<eacp::Processes::EnvironmentVariable>& environment = {});

void createBareMirror(const eacp::FilePath& mirror, const std::string& url);

bool hasCommit(const eacp::FilePath& mirror, const std::string& commit);

// The commit `ref` names on the remote now: a branch, a tag or a full sha.
std::string fetchRefIntoMirror(const eacp::FilePath& mirror, const std::string& ref);

void checkOutCommit(const eacp::FilePath& mirror,
                    const std::string& commit,
                    const eacp::FilePath& destination);

struct Submodule
{
    std::string path;
    std::string url;
    std::string commit;
};

// The submodules `commit` records, read from the .gitmodules checked out into
// `tree`, with relative URLs resolved against `parentUrl`.
eacp::Vector<Submodule> listSubmodules(const eacp::FilePath& mirror,
                                       const std::string& commit,
                                       const eacp::FilePath& tree,
                                       const std::string& parentUrl);

void applyPatch(const eacp::FilePath& tree, const eacp::FilePath& patch);
} // namespace lockstep
