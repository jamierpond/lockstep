#include "Commands.h"
#include "LockFile.h"
#include "PackageCache.h"

#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/StdPath.h>

#include <filesystem>
#include <stdexcept>

namespace lockstep
{
namespace
{
constexpr auto lockedMode = std::string_view {"locked"};
constexpr auto updateMode = std::string_view {"update"};
constexpr auto offlineMode = std::string_view {"offline"};

void printStatus(const std::string& line)
{
    eacp::Console::printError("lockstep: " + line);
}

PackageCache createPackageCache(const CommandOptions& options)
{
    return PackageCache {options.cache, options.cmake};
}

std::string getPathForLock(const eacp::FilePath& lockPath, const std::string& patch)
{
    auto lockDirectory = eacp::toStdPath(lockPath).parent_path();
    auto relative =
        eacp::toStdPath(eacp::FilePath {patch}).lexically_relative(lockDirectory);

    if (relative.empty() || *relative.begin() == "..")
        return patch;

    return eacp::FilePath {relative}.str();
}

eacp::FilePath getPatchFileFromLock(const eacp::FilePath& lockPath,
                                    const std::string& patch)
{
    auto patchPath = eacp::FilePath {patch};

    if (patchPath.isAbsolute())
        return patchPath;

    return lockPath.parentDirectory() / patch;
}

std::vector<std::string> getPatchesForLock(const CommandOptions& options)
{
    auto patches = std::vector<std::string> {};

    for (const auto& patch: options.patches)
        patches.push_back(getPathForLock(options.lock, patch));

    return patches;
}

PackageRequest createRequestFromLock(const eacp::FilePath& lockPath,
                                     const LockedPackage& package)
{
    auto request = PackageRequest {
        package.name, package.kind, package.url, package.requestedRef, {}};

    for (const auto& patch: package.patches)
        request.patches.add(getPatchFileFromLock(lockPath, patch));

    return request;
}

std::string describeRequester(const CommandOptions& options)
{
    if (options.requester.empty())
        return "the project";

    return options.requester;
}

void checkRequestMatchesLock(const CommandOptions& options,
                             const LockedPackage& locked)
{
    auto patches = getPatchesForLock(options);

    if (locked.url == options.url && locked.requestedRef == options.ref
        && locked.kind == options.kind && locked.patches == patches)
        return;

    throw std::runtime_error(
        describeRequester(options) + " asks for " + options.name + " from "
        + options.url + " at '" + options.ref + "', but " + options.lock.str()
        + " locks it from " + locked.url + " at '" + locked.requestedRef
        + "' (or with other patches). Configure with -DLOCKSTEP_MODE=update to re-lock it.");
}

LockedPackage resolvePackage(const PackageCache& cache,
                             const PackageRequest& request,
                             const std::vector<std::string>& patchesForLock)
{
    printStatus("resolving " + request.name + " at '" + request.ref + "' from "
                + request.url);
    auto materialized = cache.materialize(request, {}, Network::allowed);
    printStatus("locked " + request.name + " at " + materialized.commit
                + " (content " + materialized.hash + ")");

    return {request.name,
            request.kind,
            request.url,
            request.ref,
            patchesForLock,
            materialized.commit,
            materialized.hash};
}

eacp::FilePath ensureLockedPackageIsCached(const PackageCache& cache,
                                           const PackageRequest& request,
                                           const LockedPackage& locked,
                                           Network network)
{
    if (cache.hasIntactEntry(locked.hash, false))
        return cache.getSourceDirectory(locked.hash);

    cache.removeEntry(locked.hash);
    printStatus("fetching " + locked.name + " " + locked.commit + " into the cache");
    auto materialized = cache.materialize(request, locked.commit, network);

    if (materialized.hash != locked.hash)
    {
        throw std::runtime_error(
            locked.name + "'s content hashes to " + materialized.hash
            + " but the lock says " + locked.hash
            + "; the source changed under the same commit or URL");
    }

    return materialized.sourceDirectory;
}

Network getNetworkForMode(const std::string& mode)
{
    if (mode == offlineMode)
        return Network::forbidden;

    return Network::allowed;
}

void checkModeIsKnown(const std::string& mode)
{
    if (mode == lockedMode || mode == updateMode || mode == offlineMode)
        return;

    throw std::runtime_error("unknown mode '" + mode
                             + "'; use locked, update or offline");
}
} // namespace

int runResolveCommand(const CommandOptions& options)
{
    checkModeIsKnown(options.mode);

    if (options.name.empty() || options.url.empty())
        throw std::runtime_error("resolve needs --name and --url");

    auto cache = createPackageCache(options);
    auto lock = readLockFile(options.lock);
    auto request =
        PackageRequest {options.name, options.kind, options.url, options.ref, {}};

    for (const auto& patch: options.patches)
        request.patches.add(eacp::FilePath {patch});

    if (options.mode == updateMode)
    {
        auto resolved = resolvePackage(cache, request, getPatchesForLock(options));
        lock.storePackage(resolved);

        if (!options.shim.empty())
            lock.shim = getPathForLock(options.lock, options.shim.str());

        writeLockFile(options.lock, lock);
        eacp::Console::print(cache.getSourceDirectory(resolved.hash).str());
        return 0;
    }

    auto locked = lock.findPackage(options.name);

    if (!locked)
    {
        throw std::runtime_error(describeRequester(options) + " asks for package "
                                 + options.name + ", which " + options.lock.str()
                                 + " does not lock. Configure with "
                                   "-DLOCKSTEP_MODE=update to add it.");
    }

    checkRequestMatchesLock(options, *locked);
    auto source = ensureLockedPackageIsCached(
        cache, request, *locked, getNetworkForMode(options.mode));
    eacp::Console::print(source.str());
    return 0;
}

int runUpdateCommand(const CommandOptions& options)
{
    auto cache = createPackageCache(options);
    auto lock = readLockFile(options.lock);
    auto updatedCount = 0;

    for (const auto& package: lock.packages)
    {
        if (!options.names.empty() && !options.names.contains(package.name))
            continue;

        auto request = createRequestFromLock(options.lock, package);
        lock.storePackage(resolvePackage(cache, request, package.patches));
        ++updatedCount;
    }

    for (const auto& name: options.names)
    {
        if (!lock.findPackage(name))
            throw std::runtime_error(name + " is not in " + options.lock.str());
    }

    writeLockFile(options.lock, lock);
    printStatus("updated " + std::to_string(updatedCount) + " package(s); root "
                + readLockFile(options.lock).root);
    return 0;
}

int runVerifyCommand(const CommandOptions& options)
{
    auto cache = createPackageCache(options);
    auto lock = readLockFile(options.lock);
    auto failedCount = 0;

    for (const auto& package: lock.packages)
    {
        if (cache.hasIntactEntry(package.hash, options.full))
        {
            printStatus("ok " + package.name + " " + package.hash);
            continue;
        }

        printStatus("FAILED " + package.name + ": "
                    + cache.getSourceDirectory(package.hash).str()
                    + " is missing or no longer hashes to " + package.hash);
        ++failedCount;
    }

    if (lock.root != lock.computeRootHash())
    {
        printStatus("FAILED: the root hash in " + options.lock.str()
                    + " does not match its packages");
        ++failedCount;
    }

    return failedCount == 0 ? 0 : 1;
}

int runRootCommand(const CommandOptions& options)
{
    eacp::Console::print(readLockFile(options.lock).computeRootHash());
    return 0;
}
} // namespace lockstep
