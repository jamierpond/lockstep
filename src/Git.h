#pragma once

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <string>

namespace lockstep
{
// Runs git with `arguments` and returns its trimmed stdout. Throws with git's
// stderr when it exits non-zero or cannot be launched.
std::string runGit(
    const eacp::Vector<std::string>& arguments,
    const eacp::Vector<eacp::Processes::EnvironmentVariable>& environment = {});

// The same, but nullopt on a non-zero exit instead of throwing.
std::optional<std::string> tryRunGit(const eacp::Vector<std::string>& arguments);

void createBareMirror(const eacp::FilePath& mirror, const std::string& url);
bool hasCommit(const eacp::FilePath& mirror, const std::string& commit);

// The commit `ref` names on the remote now: a branch, a tag or a full sha.
std::string fetchRefIntoMirror(const eacp::FilePath& mirror, const std::string& ref);

void checkOutCommit(const eacp::FilePath& mirror,
                    const std::string& commit,
                    const eacp::FilePath& destination);

void applyPatch(const eacp::FilePath& tree, const eacp::FilePath& patch);
} // namespace lockstep
