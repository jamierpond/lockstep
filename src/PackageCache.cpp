#include "PackageCache.h"
#include "Download.h"
#include "Git.h"
#include "MerkleHash.h"
#include "Messages.h"
#include "Sha256.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/Zip.h>
#include <eacp/Core/Platform/Platform.h>

#include <algorithm>
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
        auto submodules = std::vector<LockedSubmodule> {};

        if (request.kind == archiveKind)
            fetchArchiveTree(request, network, tree);
        else
            resolvedCommit =
                fetchGitTree(request, commit, network, tree, submodules);

        auto contentRoot = tree;

        if (request.kind == archiveKind)
            contentRoot = findArchiveContentRoot(tree);

        for (const auto& patch: request.patches)
            applyPatch(contentRoot, patch);

        auto stored = storeTree(contentRoot, resolvedCommit, std::move(submodules));
        eacp::Files::removeAll(staging);
        return stored;
    }
    catch (...)
    {
        eacp::Files::removeAll(staging);
        throw;
    }
}

eacp::FilePath PackageCache::fetchCommitIntoMirror(const std::string& name,
                                                   const std::string& url,
                                                   const std::string& commit,
                                                   Network network) const
{
    auto mirror = getMirrorDirectory(url);
    eacp::Files::createDirectories(mirror.parentDirectory());
    createBareMirror(mirror, url);

    if (hasCommit(mirror, commit))
        return mirror;

    if (network == Network::forbidden)
    {
        throw std::runtime_error(name + " commit " + commit + " from " + url
                                 + " is not in the cache and offline mode forbids "
                                   "fetching it");
    }

    try
    {
        fetchRefIntoMirror(mirror, commit);
    }
    catch (const std::runtime_error&)
    {
        throw std::runtime_error(describeFailedDownload({name, url, commit}));
    }

    return mirror;
}

std::string
    PackageCache::fetchGitTree(const PackageRequest& request,
                               const std::string& commit,
                               Network network,
                               const eacp::FilePath& staging,
                               std::vector<LockedSubmodule>& submodules) const
{
    auto resolvedCommit = commit;
    auto mirror = getMirrorDirectory(request.url);

    if (resolvedCommit.empty())
    {
        if (network == Network::forbidden)
        {
            throw std::runtime_error(
                "resolving " + request.name + "'s ref '" + request.ref
                + "' needs the network, which offline mode forbids");
        }

        eacp::Files::createDirectories(mirror.parentDirectory());
        createBareMirror(mirror, request.url);
        resolvedCommit = fetchRefIntoMirror(mirror, request.ref);
    }
    else
    {
        mirror = fetchCommitIntoMirror(
            request.name, request.url, resolvedCommit, network);
    }

    checkOutCommit(mirror, resolvedCommit, staging);
    checkOutSubmodules(request.name,
                       request.url,
                       mirror,
                       resolvedCommit,
                       staging,
                       {},
                       request.submoduleSelection,
                       network,
                       submodules);
    return resolvedCommit;
}

void PackageCache::checkOutSubmodules(const std::string& name,
                                      const std::string& parentUrl,
                                      const eacp::FilePath& parentMirror,
                                      const std::string& parentCommit,
                                      const eacp::FilePath& tree,
                                      const std::string& pathPrefix,
                                      const SubmoduleSelection& selection,
                                      Network network,
                                      std::vector<LockedSubmodule>& submodules) const
{
    for (const auto& submodule: listSubmodules(parentMirror, parentCommit, tree))
    {
        auto fullPath = pathPrefix + submodule.path;
        auto isSelected =
            !selection.isRestricted
            || std::find(selection.paths.begin(), selection.paths.end(), fullPath)
                   != selection.paths.end();

        if (!isSelected)
            continue;

        auto url = resolveSubmoduleUrl(parentUrl, submodule.url);
        auto mirror = fetchCommitIntoMirror(name, url, submodule.commit, network);
        auto destination = tree / submodule.path;
        eacp::Files::createDirectories(destination);
        checkOutCommit(mirror, submodule.commit, destination);
        submodules.push_back({fullPath, url, submodule.commit});

        if (selection.isRecursive)
        {
            checkOutSubmodules(name,
                               url,
                               mirror,
                               submodule.commit,
                               destination,
                               fullPath + "/",
                               {},
                               network,
                               submodules);
        }
    }
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

        try
        {
            downloadFile(request.url, archive);
        }
        catch (const std::runtime_error& error)
        {
            eacp::Console::printError(
                eacp::Strings::concat("lockstep: ", error.what()));
            throw std::runtime_error(
                describeFailedDownload({request.name, request.url, {}}));
        }
    }

    if (!request.archiveSha256.empty())
    {
        auto actual = convertDigestToHex(computeFileSha256(archive));

        if (actual != request.archiveSha256)
        {
            eacp::Files::removeFile(archive);
            throw std::runtime_error(request.url + " hashes to SHA256=" + actual
                                     + ", not the URL_HASH SHA256="
                                     + request.archiveSha256);
        }
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

MaterializedPackage
    PackageCache::storeTree(const eacp::FilePath& tree,
                            const std::string& commit,
                            std::vector<LockedSubmodule> submodules) const
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
    return {commit, hash, destination, std::move(submodules)};
}
} // namespace lockstep
