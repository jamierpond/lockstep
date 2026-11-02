#include "Commands.h"
#include "CMakeSnippet.h"
#include "LockFile.h"
#include "PackageArguments.h"
#include "PackageCache.h"
#include "Session.h"

#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/Strings.h>

#include <ResEmbed/ResEmbed.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace lockstep
{
namespace
{
constexpr auto lockedMode = std::string_view {"locked"};
constexpr auto updateMode = std::string_view {"update"};
constexpr auto offlineMode = std::string_view {"offline"};
constexpr auto overrideSuffix = std::string_view {"_SOURCE"};
constexpr auto overridePrefix = std::string_view {"LOCKSTEP_"};

void printStatus(const std::string& line)
{
    eacp::Console::printError("lockstep: " + line);
}

std::string getPathForLock(const eacp::FilePath& lockPath, const std::string& path)
{
    auto lockDirectory = eacp::toStdPath(lockPath).parent_path();
    auto relative =
        eacp::toStdPath(eacp::FilePath {path}).lexically_relative(lockDirectory);

    if (relative.empty())
        return path;

    return eacp::FilePath {relative}.str();
}

// Only a bootstrap inside the project is recorded, so the lock's bytes never
// depend on where the project or anything outside it sits.
std::string getBootstrapPathForLock(const CommandOptions& options)
{
    auto relative = getPathForLock(options.lock, options.bootstrap.str());

    if (relative.starts_with("..") || eacp::FilePath {relative}.isAbsolute())
        return {};

    return relative;
}

eacp::FilePath getPathFromLock(const eacp::FilePath& lockPath,
                               const std::string& path)
{
    auto asPath = eacp::FilePath {path};

    if (asPath.isAbsolute())
        return asPath;

    return lockPath.parentDirectory() / path;
}

eacp::FilePath makeAbsolute(const eacp::FilePath& base, const std::string& path)
{
    auto asPath = eacp::FilePath {path};

    if (asPath.isAbsolute())
        return asPath;

    return eacp::FilePath {
        (eacp::toStdPath(base) / eacp::toStdPath(asPath)).lexically_normal()};
}

PackageRequest createRequestFromLock(const eacp::FilePath& lockPath,
                                     const LockedPackage& package)
{
    auto request = PackageRequest {
        package.name, package.kind, package.url, package.requestedRef, {}};
    request.submoduleSelection = package.submoduleSelection;

    for (const auto& patch: package.patches)
        request.patches.add(getPathFromLock(lockPath, patch));

    return request;
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
            request.submoduleSelection,
            materialized.commit,
            materialized.submodules,
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

    for (const auto& submodule: materialized.submodules)
    {
        auto isLocked = std::any_of(locked.submodules.begin(),
                                    locked.submodules.end(),
                                    [&](const auto& entry)
                                    { return isSameSubmodule(entry, submodule); });

        if (!isLocked)
        {
            throw std::runtime_error(
                locked.name + "'s submodule " + submodule.path + " is at "
                + submodule.commit + " from " + submodule.url
                + ", which the lock does not record; re-lock it "
                  "with -DLOCKSTEP_MODE=update");
        }
    }

    if (materialized.submodules.size() != locked.submodules.size())
    {
        throw std::runtime_error(locked.name + " checks out "
                                 + std::to_string(materialized.submodules.size())
                                 + " submodules, but the lock records "
                                 + std::to_string(locked.submodules.size()));
    }

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

    throw std::runtime_error("LOCKSTEP_MODE is '" + mode
                             + "'; use locked, update or offline");
}

PackageCache createPackageCache(const CommandOptions& options)
{
    return PackageCache {options.cache, options.cmake};
}

std::vector<std::string> getPatchesForLock(const CommandOptions& options,
                                           const PackageSpec& spec)
{
    auto patches = std::vector<std::string> {};

    for (const auto& patch: spec.patches)
    {
        auto absolute = makeAbsolute(options.requestedBy, patch);
        patches.push_back(getPathForLock(options.lock, absolute.str()));
    }

    return patches;
}

void checkRequestMatchesLock(const CommandOptions& options,
                             const PackageSpec& spec,
                             const LockedPackage& locked)
{
    if (locked.url == spec.url && locked.requestedRef == spec.ref
        && locked.kind == spec.kind
        && locked.patches == getPatchesForLock(options, spec)
        && isSameSelection(locked.submoduleSelection, spec.submoduleSelection))
        return;

    throw std::runtime_error(
        options.requestedBy.str() + " asks for " + spec.name + " from " + spec.url
        + " at '" + spec.ref + "', but " + options.lock.str() + " locks it from "
        + locked.url + " at '" + locked.requestedRef
        + "' (or with other patches or submodules). Configure with -DLOCKSTEP_MODE=update to "
          "re-lock it.");
}

eacp::FilePath resolveLockedSource(const CommandOptions& options,
                                   const PackageSpec& spec)
{
    checkModeIsKnown(options.mode);

    auto cache = createPackageCache(options);
    auto lock = readLockFile(options.lock);
    auto request = PackageRequest {spec.name, spec.kind, spec.url, spec.ref, {}};
    request.submoduleSelection = spec.submoduleSelection;
    request.archiveSha256 = spec.archiveSha256;

    for (const auto& patch: spec.patches)
        request.patches.add(makeAbsolute(options.requestedBy, patch));

    if (options.mode == updateMode)
    {
        auto resolved =
            resolvePackage(cache, request, getPatchesForLock(options, spec));
        lock.storePackage(resolved);
        lock.bootstrap = getBootstrapPathForLock(options);
        writeLockFile(options.lock, lock);
        return cache.getSourceDirectory(resolved.hash);
    }

    auto locked = lock.findPackage(spec.name);

    if (!locked)
    {
        throw std::runtime_error(options.requestedBy.str() + " asks for package "
                                 + spec.name + ", which " + options.lock.str()
                                 + " does not lock. Configure with "
                                   "-DLOCKSTEP_MODE=update to add it.");
    }

    checkRequestMatchesLock(options, spec, *locked);
    return ensureLockedPackageIsCached(
        cache, request, *locked, getNetworkForMode(options.mode));
}

eacp::FilePath resolvePackageSource(const CommandOptions& options,
                                    const PackageSpec& spec,
                                    const Session& session)
{
    auto overridden = session.findOverride(spec.name);

    if (!overridden)
        overridden = eacp::getEnv("LOCKSTEP_" + spec.name + "_SOURCE");

    if (overridden && !overridden->empty())
    {
        if (options.isCI && options.mode == lockedMode)
        {
            throw std::runtime_error("LOCKSTEP_" + spec.name + "_SOURCE overrides "
                                     + spec.name + " with " + *overridden
                                     + ", which LOCKSTEP_CI forbids in locked mode");
        }

        printStatus("WARNING: " + spec.name
                    + " comes from the local override LOCKSTEP_" + spec.name
                    + "_SOURCE=" + *overridden + ", not from the lock");
        return makeAbsolute(options.sourceDirectory, *overridden);
    }

    if (spec.kind == "local")
        return makeAbsolute(options.requestedBy, spec.localSourceDirectory);

    return resolveLockedSource(options, spec);
}

std::string getBinaryDirectory(const CommandOptions& options,
                               const std::string& name)
{
    auto lowerName = eacp::Strings::toLower(name);
    return (options.buildDirectory / "_deps" / (lowerName + "-build")).str();
}

std::string writeVariable(const std::string& name, const std::string& value)
{
    return "set(" + name + " " + quoteForCMake(value) + ")\n";
}

std::string writePackageVariables(const SessionPackage& package, bool isAdded)
{
    auto snippet = std::string {};
    snippet += writeVariable(package.name + "_SOURCE_DIR", package.sourceDirectory);
    snippet += writeVariable(package.name + "_BINARY_DIR", package.binaryDirectory);

    if (isAdded)
        snippet += writeVariable(package.name + "_ADDED", "YES");
    else
        snippet += writeVariable(package.name + "_ADDED", "NO");

    snippet += writeVariable("CPM_LAST_PACKAGE_NAME", package.name);
    snippet += writeVariable("CPM_PACKAGE_" + package.name + "_SOURCE_DIR",
                             package.sourceDirectory);
    snippet += writeVariable("CPM_PACKAGE_" + package.name + "_BINARY_DIR",
                             package.binaryDirectory);
    return snippet;
}

std::string writeAddSubdirectory(const PackageSpec& spec,
                                 const SessionPackage& package)
{
    auto subdirectory = eacp::FilePath {package.sourceDirectory};

    if (!spec.sourceSubdirectory.empty())
        subdirectory = subdirectory / spec.sourceSubdirectory;

    if (spec.isDownloadOnly || !eacp::Files::isFile(subdirectory / "CMakeLists.txt"))
        return {};

    auto snippet = std::string {};

    for (const auto& [name, value]: spec.options)
        snippet += writeVariable(name, value);

    snippet += "set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)\n";
    snippet += "add_subdirectory(" + quoteForCMake(subdirectory.str()) + " "
               + quoteForCMake(package.binaryDirectory);

    if (spec.isExcludedFromAll)
        snippet += " EXCLUDE_FROM_ALL";

    if (spec.isSystem)
        snippet += " SYSTEM";

    return snippet + ")\n";
}

// What CPM writes after adding a package, so a later find_package(<Name>) in
// config mode finds the package already added instead of an installed one.
std::string writeFindPackageRedirect(const PackageSpec& spec)
{
    if (spec.isDownloadOnly)
        return {};

    auto lowerName = eacp::Strings::toLower(spec.name);
    auto redirects =
        std::string {"${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/"} + lowerName;
    return "file(WRITE \"" + redirects
           + "-config.cmake\" \"include(\\\"\\${CMAKE_CURRENT_LIST_DIR}/" + lowerName
           + "-extra.cmake\\\" OPTIONAL)\\n\")\n" + "file(WRITE \"" + redirects
           + "-config-version.cmake\" \"set(PACKAGE_VERSION_COMPATIBLE TRUE)\\n"
             "set(PACKAGE_VERSION_EXACT TRUE)\\n\")\n";
}

std::string writePropagatingBlock(const std::string& name, const std::string& body)
{
    return "block(SCOPE_FOR VARIABLES PROPAGATE " + name + "_SOURCE_DIR " + name
           + "_BINARY_DIR " + name + "_ADDED CPM_LAST_PACKAGE_NAME CPM_PACKAGE_"
           + name + "_SOURCE_DIR CPM_PACKAGE_" + name + "_BINARY_DIR)\n" + body
           + "endblock()\n";
}

std::string addPackage(const CommandOptions& options, const PackageSpec& spec)
{
    auto session = readSession(options.buildDirectory);

    if (auto added = session.findPackage(spec.name))
    {
        if (added->requestedRef != spec.ref)
        {
            printStatus("WARNING: " + options.requestedBy.str() + " asks for "
                        + spec.name + " at '" + spec.ref
                        + "', but it was already added at '" + added->requestedRef
                        + "', which is kept");
        }

        return writePropagatingBlock(spec.name,
                                     writePackageVariables(*added, false));
    }

    auto package =
        SessionPackage {spec.name,
                        spec.ref,
                        resolvePackageSource(options, spec, session).str(),
                        getBinaryDirectory(options, spec.name)};
    session.packages.push_back(package);
    writeSession(options.buildDirectory, session);

    return writePropagatingBlock(spec.name,
                                 writePackageVariables(package, true)
                                     + writeAddSubdirectory(spec, package)
                                     + writeFindPackageRedirect(spec));
}

void writeSnippet(const CommandOptions& options, const std::string& snippet)
{
    eacp::Files::createDirectories(options.output.parentDirectory());
    eacp::Files::writeFile(options.output, std::string_view {snippet});
}

bool isSameFile(const eacp::FilePath& first, const std::string& second)
{
    auto error = std::error_code {};
    return std::filesystem::equivalent(
        eacp::toStdPath(first), eacp::toStdPath(eacp::FilePath {second}), error);
}

void checkLoadedDuringProject(const CommandOptions& options)
{
    auto entries = options.topLevelIncludes;
    auto start = std::size_t {0};

    while (start <= entries.size())
    {
        auto end = entries.find(';', start);

        if (end == std::string::npos)
            end = entries.size();

        if (isSameFile(options.bootstrap, entries.substr(start, end - start)))
            return;

        start = end + 1;
    }

    throw std::runtime_error(
        options.bootstrap.str()
        + " was include()d, which is too late to route FetchContent_MakeAvailable "
          "through lockstep. Load it during the top-level project() instead: put\n"
          "  set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/<path>/"
          "lockstep.cmake)\nbefore project() in "
        + options.sourceDirectory.str()
        + "/CMakeLists.txt and remove the include().");
}

bool isOverrideVariable(const std::string& variable)
{
    return variable.size() > overridePrefix.size() + overrideSuffix.size()
           && variable.starts_with(overridePrefix)
           && variable.ends_with(overrideSuffix);
}

SessionOverride parseOverride(const std::string& definition)
{
    auto equals = definition.find('=');
    auto variable = definition.substr(0, equals);

    if (equals == std::string::npos || !isOverrideVariable(variable))
    {
        throw std::runtime_error(
            "--override takes LOCKSTEP_<Name>_SOURCE=<dir>, not " + definition);
    }

    auto nameLength =
        variable.size() - overridePrefix.size() - overrideSuffix.size();
    return {variable.substr(overridePrefix.size(), nameLength),
            definition.substr(equals + 1)};
}
} // namespace

namespace
{
PackageSpec parseArgumentsFrom(const CommandOptions& options,
                               ArgumentDialect dialect)
{
    try
    {
        return parsePackageArguments(options.packageArguments, dialect);
    }
    catch (const std::invalid_argument& error)
    {
        throw std::runtime_error(options.requestedBy.str() + ": " + error.what());
    }
}
} // namespace

int runBeginCommand(const CommandOptions& options)
{
    checkModeIsKnown(options.mode);
    checkLoadedDuringProject(options);

    auto session = Session {};

    for (const auto& definition: options.overrides)
    {
        auto entry = parseOverride(definition);

        if (!entry.sourceDirectory.empty())
            session.overrides.push_back(entry);
    }

    writeSession(options.buildDirectory, session);
    return 0;
}

int runAddCommand(const CommandOptions& options)
{
    auto spec = parseArgumentsFrom(options, ArgumentDialect::cpm);
    writeSnippet(options, addPackage(options, spec));
    return 0;
}

int runProvideCommand(const CommandOptions& options)
{
    auto spec = parseArgumentsFrom(options, ArgumentDialect::fetchContent);

    if (spec.kind == "local")
    {
        writeSnippet(options, {});
        return 0;
    }

    auto snippet = addPackage(options, spec);
    snippet += "FetchContent_SetPopulated(" + spec.name + " SOURCE_DIR \"${"
               + spec.name + "_SOURCE_DIR}\" BINARY_DIR \"${" + spec.name
               + "_BINARY_DIR}\")\n";
    writeSnippet(options, snippet);
    return 0;
}

int runUpdateCommand(const CommandOptions& options)
{
    auto cache = createPackageCache(options);
    auto lock = readLockFile(options.lock);
    auto updatedCount = 0;

    for (const auto& name: options.names)
    {
        if (!lock.findPackage(name))
            throw std::runtime_error(name + " is not in " + options.lock.str());
    }

    for (const auto& package: lock.packages)
    {
        if (!options.names.empty() && !options.names.contains(package.name))
            continue;

        auto request = createRequestFromLock(options.lock, package);
        lock.storePackage(resolvePackage(cache, request, package.patches));
        ++updatedCount;
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

    if (failedCount > 0)
        return 1;

    return 0;
}

int runRootCommand(const CommandOptions& options)
{
    if (!options.announce)
    {
        eacp::Console::print(readLockFile(options.lock).computeRootHash());
        return 0;
    }

    if (eacp::Files::isFile(options.lock))
        eacp::Console::print("-- lockstep: root "
                             + readLockFile(options.lock).computeRootHash());

    return 0;
}

int runCMakeModuleCommand(const CommandOptions& options)
{
    auto module = ResEmbed::get("lockstep-module.cmake", "LockstepModule");

    if (!module)
        throw std::runtime_error("this lockstep was built without its CMake module");

    eacp::Files::createDirectories(options.output.parentDirectory());
    eacp::Files::writeFile(options.output, module.toStringView());
    return 0;
}
} // namespace lockstep
