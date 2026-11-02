#pragma once

#include <Miro/Reflect.h>

#include <string>
#include <vector>

namespace lockstep
{
// Which submodules a git package checks out: every one by default, as CPM
// does, or only `paths` when GIT_SUBMODULES named them (none when it was
// given empty).
struct SubmoduleSelection
{
    bool isRestricted = false;
    std::vector<std::string> paths;
    bool isRecursive = true;

    MIRO_REFLECT(isRestricted, paths, isRecursive)
};

struct LockedSubmodule
{
    std::string path;
    std::string url;
    std::string commit;

    MIRO_REFLECT(path, url, commit)
};

// A .gitmodules URL made absolute: "../b.git" or "./b" resolved against the
// URL of the repository that lists it, as git does.
std::string resolveSubmoduleUrl(const std::string& parentUrl,
                                const std::string& url);
} // namespace lockstep
