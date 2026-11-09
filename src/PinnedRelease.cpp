#include "PinnedRelease.h"
#include "Download.h"
#include "LockFile.h"
#include "FileHash.h"

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <filesystem>
#include <regex>
#include <stdexcept>

namespace lockstep
{
namespace
{
std::string getHostSystemName()
{
    if constexpr (eacp::Platform::isMac())
        return "macos";

    if constexpr (eacp::Platform::isWindows())
        return "windows";

    return "linux";
}

std::string getHostArchitectureName()
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#else
    return "x64";
#endif
}

std::optional<std::string> findSetValue(const std::string& text,
                                        const std::string& variable)
{
    auto pattern =
        std::regex("set\\(\\s*" + variable + "\\s+\"?([^\")\\s]*)\"?\\s*\\)");
    auto match = std::smatch {};

    if (!std::regex_search(text, match, pattern))
        return std::nullopt;

    return match[1].str();
}

std::string getAssetName(const PinnedRelease& release)
{
    auto name = "lockstep-" + release.version + "-" + getHostPlatformName();

    if constexpr (eacp::Platform::isWindows())
        name += ".exe";

    return name;
}

void makeExecutable(const eacp::FilePath& file)
{
    using std::filesystem::perms;
    std::filesystem::permissions(eacp::toStdPath(file),
                                 perms::owner_exec | perms::group_exec
                                     | perms::others_exec,
                                 std::filesystem::perm_options::add);
}
} // namespace

std::string getHostPlatformName()
{
    return getHostSystemName() + "-" + getHostArchitectureName();
}

std::optional<eacp::FilePath> findEnclosingLockFile(const eacp::FilePath& directory)
{
    auto current = eacp::toStdPath(directory);

    while (!current.empty())
    {
        auto candidate = current / lockFileName;

        if (eacp::Files::isFile(eacp::FilePath {candidate}))
            return eacp::FilePath {candidate};

        if (current == current.parent_path())
            break;

        current = current.parent_path();
    }

    return std::nullopt;
}

std::optional<PinnedRelease> readPinnedRelease(const eacp::FilePath& shim)
{
    if (!eacp::Files::isFile(shim))
        return std::nullopt;

    auto text = eacp::Files::readFile(shim);
    auto version = findSetValue(text, "LOCKSTEP_VERSION");
    auto releaseUrl = findSetValue(text, "LOCKSTEP_RELEASE_URL");
    auto sha256 = findSetValue(text, "LOCKSTEP_SHA256_" + getHostPlatformName());

    if (!version || !releaseUrl)
        return std::nullopt;

    return PinnedRelease {*version, sha256.value_or(""), *releaseUrl};
}

eacp::FilePath fetchPinnedExecutable(const PinnedRelease& release,
                                     const eacp::FilePath& cache)
{
    if (release.sha256.empty())
    {
        throw std::runtime_error("the shim pins no " + getHostPlatformName()
                                 + " binary for lockstep " + release.version);
    }

    auto asset = getAssetName(release);
    auto executable = cache / "bin" / release.version / asset;

    if (!eacp::Files::isFile(executable))
    {
        auto url = release.releaseUrl + "/v" + release.version + "/" + asset;
        downloadFile(url, executable, {.hex = release.sha256});
        makeExecutable(executable);
    }

    auto actualHash = computeFileSha256HexOrThrow(executable);

    if (actualHash != release.sha256)
    {
        throw std::runtime_error(executable.str() + " hashes to " + actualHash
                                 + ", not the pinned " + release.sha256
                                 + "; delete it to download it again");
    }

    return executable;
}
} // namespace lockstep
