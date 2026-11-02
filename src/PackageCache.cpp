#include "PackageCache.h"
#include "Download.h"
#include "Git.h"
#include "MerkleHash.h"
#include "Sha256.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/Zip.h>
#include <eacp/Core/Platform/Platform.h>

#include <stdexcept>

namespace lockstep
{
namespace
{
constexpr auto urlKeyLength = 16;
constexpr auto archiveKind = std::string_view {"archive"};

std::string computeUrlKey(const std::string& url)
{
    return computeSha256Hex(url).substr(0, urlKeyLength);
}

eacp::FilePath getStampPath(const eacp::FilePath& sourceDirectory)
{
    return sourceDirectory.sibling(sourceDirectory.fileName() + ".stamp");
}

void writeTextFile(const eacp::FilePath& path, const std::string& text)
{
    eacp::Files::writeFile(path, std::string_view {text});
}

std::string readStamp(const eacp::FilePath& path)
{
    if (!eacp::Files::isFile(path))
        return {};

    return eacp::Strings::trim(eacp::Files::readFile(path));
}

eacp::FilePath findArchiveContentRoot(const eacp::FilePath& extracted)
{
    auto options = eacp::Files::DirectoryOptions {};
    options.includeHidden = true;
    auto entries = eacp::Files::listDirectory(extracted, options);

    if (entries.size() == 1 && entries[0].kind == eacp::Files::EntryKind::directory)
        return entries[0].path;

    return extracted;
}
} // namespace

PackageCache::PackageCache(eacp::FilePath rootToUse, eacp::FilePath cmakeToUse)
    : root(std::move(rootToUse))
    , cmake(std::move(cmakeToUse))
{
}

eacp::FilePath PackageCache::getDefaultDirectory()
{
    if constexpr (eacp::Platform::isWindows())
        return eacp::FilePath::cacheDirectory() / "lockstep";

    if (auto xdgCache = eacp::getEnv("XDG_CACHE_HOME");
        xdgCache && !xdgCache->empty())
        return eacp::FilePath {*xdgCache} / "lockstep";

    return eacp::FilePath::homeDirectory() / ".cache" / "lockstep";
}

eacp::FilePath PackageCache::getSourceDirectory(const std::string& hash) const
{
    return root / "src" / hash;
}

eacp::FilePath PackageCache::getMirrorDirectory(const std::string& url) const
{
    return root / "git" / (computeUrlKey(url) + ".git");
}

eacp::FilePath PackageCache::getDownloadPath(const std::string& url) const
{
    auto name = eacp::Files::legalFileName(eacp::FilePath {url}.fileName());
    return root / "downloads" / computeUrlKey(url) / name;
}

eacp::FilePath PackageCache::createStagingDirectory() const
{
    auto stagingRoot = root / "staging";
    eacp::Files::createDirectories(stagingRoot);

    auto staging = eacp::Files::nonexistentChild(stagingRoot, "tree", "");
    eacp::Files::createDirectories(staging);
    return staging;
}

bool PackageCache::hasIntactEntry(const std::string& hash,
                                  bool alwaysRecompute) const
{
    auto directory = getSourceDirectory(hash);

    if (!eacp::Files::isDirectory(directory))
        return false;

    auto stampPath = getStampPath(directory);
    auto stamp = computeTreeStamp(directory);

    if (!alwaysRecompute && readStamp(stampPath) == stamp)
        return true;

    if (computeMerkleHash(directory) != hash)
        return false;

    writeTextFile(stampPath, stamp);
    return true;
}

void PackageCache::removeEntry(const std::string& hash) const
{
    auto directory = getSourceDirectory(hash);
    eacp::Files::removeAll(directory);
    eacp::Files::removeFile(getStampPath(directory));
}

MaterializedPackage PackageCache::materialize(const PackageRequest& request,
                                              const std::string& commit,
                                              Network network) const
{
    auto staging = createStagingDirectory();

    try
    {
        auto tree = staging / "tree";
        eacp::Files::createDirectories(tree);
        auto resolvedCommit = std::string {};

        if (request.kind == archiveKind)
            fetchArchiveTree(request, network, tree);
        else
            resolvedCommit = fetchGitTree(request, commit, network, tree);

        auto contentRoot = tree;

        if (request.kind == archiveKind)
            contentRoot = findArchiveContentRoot(tree);

        for (const auto& patch: request.patches)
            applyPatch(contentRoot, patch);

        auto stored = storeTree(contentRoot, resolvedCommit);
        eacp::Files::removeAll(staging);
        return stored;
    }
    catch (...)
    {
        eacp::Files::removeAll(staging);
        throw;
    }
}

std::string PackageCache::fetchGitTree(const PackageRequest& request,
                                       const std::string& commit,
                                       Network network,
                                       const eacp::FilePath& staging) const
{
    auto mirror = getMirrorDirectory(request.url);
    eacp::Files::createDirectories(mirror.parentDirectory());
    createBareMirror(mirror, request.url);

    auto resolvedCommit = commit;

    if (resolvedCommit.empty())
    {
        if (network == Network::forbidden)
            throw std::runtime_error(
                "resolving " + request.name + "'s ref '" + request.ref
                + "' needs the network, which offline mode forbids");

        resolvedCommit = fetchRefIntoMirror(mirror, request.ref);
    }
    else if (!hasCommit(mirror, resolvedCommit))
    {
        if (network == Network::forbidden)
        {
            throw std::runtime_error(
                request.name + " commit " + resolvedCommit
                + " is not in the cache and offline mode forbids "
                  "fetching it");
        }

        fetchRefIntoMirror(mirror, resolvedCommit);
    }

    checkOutCommit(mirror, resolvedCommit, staging);
    return resolvedCommit;
}

void PackageCache::fetchArchiveTree(const PackageRequest& request,
                                    Network network,
                                    const eacp::FilePath& staging) const
{
    auto archive = getDownloadPath(request.url);

    if (!eacp::Files::isFile(archive))
    {
        if (network == Network::forbidden)
        {
            throw std::runtime_error(request.name + "'s archive " + request.url
                                     + " is not in the cache and offline mode "
                                       "forbids downloading it");
        }

        downloadFile(request.url, archive);
    }

    extractArchive(archive, staging);
}

void PackageCache::extractArchive(const eacp::FilePath& archive,
                                  const eacp::FilePath& destination) const
{
    if (archive.hasExtension("zip"))
    {
        auto reader = eacp::Zip::Reader {archive};

        if (!reader.isValid() || !reader.extractAll(destination))
            throw std::runtime_error("cannot extract " + archive.str() + ": "
                                     + reader.errorMessage());
        return;
    }

    auto options = eacp::Processes::ProcessOptions {};
    options.executable = cmake.str();
    options.arguments = {"-E", "tar", "xf", archive.str()};
    options.workingDirectory = destination.str();
    auto result = eacp::Processes::run(options);

    if (result.exitCode != 0)
        throw std::runtime_error("cmake -E tar could not extract " + archive.str()
                                 + ":\n" + result.errorOutput);
}

MaterializedPackage PackageCache::storeTree(const eacp::FilePath& tree,
                                            const std::string& commit) const
{
    auto hash = computeMerkleHash(tree);
    auto destination = getSourceDirectory(hash);

    if (!eacp::Files::isDirectory(destination))
    {
        eacp::Files::createDirectories(destination.parentDirectory());

        if (!eacp::Files::moveFile(tree, destination)
            && !eacp::Files::isDirectory(destination))
            throw std::runtime_error("cannot move the unpacked tree into "
                                     + destination.str());
    }

    writeTextFile(getStampPath(destination), computeTreeStamp(destination));
    return {commit, hash, destination};
}
} // namespace lockstep
