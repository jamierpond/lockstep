#include "Submodules.h"

namespace lockstep
{
namespace
{
constexpr auto parentPrefix = std::string_view {"../"};
constexpr auto currentPrefix = std::string_view {"./"};

std::string removeLastComponent(std::string url)
{
    while (!url.empty() && url.back() == '/')
        url.pop_back();

    auto separator = url.find_last_of("/:");

    if (separator == std::string::npos)
        return {};

    return url.substr(0, separator);
}

struct RelativeUrlWalk
{
    std::string base;
    std::string_view rest;

    bool hasRelativeStep() const
    {
        return rest.starts_with(parentPrefix) || rest.starts_with(currentPrefix);
    }

    void takeStep()
    {
        if (rest.starts_with(parentPrefix))
        {
            base = removeLastComponent(base);
            rest.remove_prefix(parentPrefix.size());
            return;
        }

        rest.remove_prefix(currentPrefix.size());
    }

    std::string join() const
    {
        if (!base.empty() && base.back() == ':')
            return base + std::string {rest};

        return base + "/" + std::string {rest};
    }
};
} // namespace

bool isSameSelection(const SubmoduleSelection& first,
                     const SubmoduleSelection& second)
{
    return first.isRestricted == second.isRestricted && first.paths == second.paths
           && first.isRecursive == second.isRecursive;
}

bool isSameSubmodule(const LockedSubmodule& first, const LockedSubmodule& second)
{
    return first.path == second.path && first.url == second.url
           && first.commit == second.commit;
}

std::string resolveSubmoduleUrl(const std::string& parentUrl, const std::string& url)
{
    auto walk = RelativeUrlWalk {parentUrl, url};

    if (!walk.hasRelativeStep())
        return url;

    while (!walk.base.empty() && walk.base.back() == '/')
        walk.base.pop_back();

    while (walk.hasRelativeStep())
        walk.takeStep();

    return walk.join();
}
} // namespace lockstep
