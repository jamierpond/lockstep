#include "Commands.h"
#include "CMakeSnippet.h"
#include "LockFile.h"
#include "Messages.h"
#include "PackageArguments.h"
#include "PackageCache.h"
#include "Session.h"

#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Format.h>
#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/Strings.h>

#include <ResEmbed/ResEmbed.h>

#include "MerkleHash.h"

#include <algorithm>
#include <array>
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
constexpr auto overridePrefixes =
    std::array<std::string_view, 2> {"LOCKSTEP_", "CPM_"};
constexpr auto archiveKind = std::string_view {"archive"};

void printStatus(const std::string& line)
{
    eacp::Console::printError("lockstep: " + line);
}

// An archive has no commit, so its content hash is what names the version.
std::string describeVersion(const std::string& kind,
                            const std::string& commit,
                            const std::string& hash)
{
    if (kind == archiveKind)
        return "content " + hash;

    return commit + " (content " + hash + ")";
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

std::string getPathForUser(const CommandOptions& options, const eacp::FilePath& path)
{
    auto projectDirectory = eacp::toStdPath(options.sourceDirectory);

    if (projectDirectory.empty())
        projectDirectory = std::filesystem::current_path();

    auto relative = eacp::toStdPath(path).lexically_relative(projectDirectory);
    auto isInsideProject = !relative.empty() && *relative.begin() != "..";

    if (!isInsideProject)
        return path.str();

    return eacp::FilePath {relative}.str();
}

eacp::FilePath getRequestingFile(const CommandOptions& options)
{
    if (eacp::Files::isFile(options.requestedByFile))
        return options.requestedByFile;

    return options.requestedBy / "CMakeLists.txt";
}

RequestLocation getRequestLocation(const CommandOptions& options,
                                   const std::string& name)
{
    auto file = getRequestingFile(options);

    if (!eacp::Files::isFile(file))
        return {getPathForUser(options, options.requestedBy), std::nullopt};

    return {getPathForUser(options, file),
            findPackageRequestLine(eacp::Files::readFile(file), name)};
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
    printStatus(
        "locked " + request.name + " at "
        + describeVersion(request.kind, materialized.commit, materialized.hash));

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

eacp::FilePath ensureLockedPackageIsCached(const CommandOptions& options,
                                           const PackageCache& cache,
                                           const PackageRequest& request,
                                           const LockedPackage& locked,
                                           Network network)
{
    if (cache.hasIntactEntry(locked.hash, false))
        return cache.getSourceDirectory(locked.hash);

    cache.removeEntry(locked.hash);
    printStatus("fetching " + locked.name + " "
                + describeVersion(locked.kind, locked.commit, locked.hash)
                + " into the cache");
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
        throw std::runtime_error(describeChangedContent(
            {locked.name, locked.url, getPathForUser(options, options.lock)}));
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

std::string describeRequest(const std::string& url,
                            const std::string& ref,
                            const std::string& otherUrl)
{
    if (url != otherUrl)
        return eacp::Strings::format("from {} at '{}'", url, ref);

    return eacp::Strings::format("at '{}'", ref);
}

void checkRequestMatchesLock(const CommandOptions& options,
                             const PackageSpec& spec,
                             const LockedPackage& locked)
{
    auto isSameSource = locked.url == spec.url && locked.requestedRef == spec.ref
                        && locked.kind == spec.kind;
    auto isSamePatches = locked.patches == getPatchesForLock(options, spec);
    auto isSameSubmodules =
        isSameSelection(locked.submoduleSelection, spec.submoduleSelection);

    if (isSameSource && isSamePatches && isSameSubmodules)
        return;

    auto mismatch =
        LockMismatch {getRequestLocation(options, spec.name),
                      spec.name,
                      getPathForUser(options, options.lock),
                      describeRequest(spec.url, spec.ref, locked.url),
                      describeRequest(locked.url, locked.requestedRef, spec.url),
                      getPathForUser(options, options.buildDirectory)};

    if (isSameSource && !isSamePatches)
    {
        mismatch.requestedDescription = "with other patches";
        mismatch.lockedDescription = "with the patches it records";
    }
    else if (isSameSource)
    {
        mismatch.requestedDescription = "with other submodules";
        mismatch.lockedDescription = "with the submodules it records";
    }

    throw std::runtime_error(describeLockMismatch(mismatch));
}

eacp::FilePath resolveLockedSource(const CommandOptions& options,
                                   const PackageSpec& spec)
{
    checkModeIsKnown(options.mode);

    auto cache = createPackageCache(options);
    auto lock = readLockFile(options.lock);
    auto request = PackageRequest {spec.name, spec.kind, spec.url, spec.ref, {}};
    request.submoduleSelection = spec.submoduleSelection;
    request.archiveHash = spec.archiveHash;

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
        throw std::runtime_error(describeUnlockedRequest(
            {getRequestLocation(options, spec.name),
             spec.name,
             getPathForUser(options, options.lock),
             eacp::Files::isFile(options.lock),
             getPathForUser(options, options.buildDirectory)}));
    }

    checkRequestMatchesLock(options, spec, *locked);
    return ensureLockedPackageIsCached(
        options, cache, request, *locked, getNetworkForMode(options.mode));
}

std::optional<SessionOverride> findOverride(const PackageSpec& spec,
                                            const Session& session)
{
    if (auto overridden = session.findOverride(spec.name))
        return overridden;

    for (auto prefix: overridePrefixes)
    {
        auto variable =
            std::string {prefix} + spec.name + std::string {overrideSuffix};

        if (auto value = eacp::getEnv(variable); value && !value->empty())
            return SessionOverride {spec.name, *value, variable};
    }

    return std::nullopt;
}

std::string getCheckoutStamp(const eacp::FilePath& checkout, const std::string& hash)
{
    return hash + " " + computeTreeStamp(checkout);
}

// SOURCE_DIR beside a repository or URL: the locked tree is copied there, and
// copied again whenever the stamp kept in the build directory stops matching.
eacp::FilePath placeCheckout(const CommandOptions& options,
                             const PackageSpec& spec,
                             const eacp::FilePath& cached)
{
    auto checkout =
        makeAbsolute(options.requestedBinaryDirectory, spec.checkoutDirectory);
    auto stampPath =
        options.buildDirectory / "_lockstep" / "checkouts" / (spec.name + ".stamp");
    auto hash = cached.fileName();

    if (eacp::Files::isFile(stampPath) && eacp::Files::isDirectory(checkout)
        && eacp::Files::readFile(stampPath) == getCheckoutStamp(checkout, hash))
        return checkout;

    printStatus("checking out " + spec.name + " into " + checkout.str());
    eacp::Files::removeAll(checkout);

    if (!eacp::Files::copyDirectory(cached, checkout))
        throw std::runtime_error(formatUserError(
            "lockstep could not copy " + spec.name + " into its SOURCE_DIR "
                + checkout.str() + ".",
            "Make " + checkout.str() + " writable, then configure again."));

    eacp::Files::createDirectories(stampPath.parentDirectory());
    eacp::Files::writeFile(stampPath,
                           std::string_view {getCheckoutStamp(checkout, hash)});
    return checkout;
}

eacp::FilePath resolvePackageSource(const CommandOptions& options,
                                    const PackageSpec& spec,
                                    const Session& session)
{
    if (auto overridden = findOverride(spec, session))
    {
        auto setting = overridden->variable + "=" + overridden->sourceDirectory;

        if (options.isCI && options.mode == lockedMode)
        {
            throw std::runtime_error(
                formatUserError(setting + " overrides " + spec.name
                                    + ", which LOCKSTEP_CI forbids in locked mode.",
                                "Unset " + overridden->variable
                                    + ", or configure without -DLOCKSTEP_CI=ON."));
        }

        printStatus("WARNING: " + spec.name + " comes from the local override "
                    + setting + ", not from the lock");
        return makeAbsolute(options.sourceDirectory, overridden->sourceDirectory);
    }

    if (spec.kind == "local")
        return makeAbsolute(options.requestedBy, spec.localSourceDirectory);

    auto cached = resolveLockedSource(options, spec);

    if (spec.checkoutDirectory.empty())
        return cached;

    return placeCheckout(options, spec, cached);
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

// Every command that includes the snippet is a function, so the package's
// variables are handed on to its caller, as CPM's cpm_export_variables does.
std::string writePropagatingBlock(const std::string& name, const std::string& body)
{
    auto propagated =
        eacp::Vector<std::string> {name + "_SOURCE_DIR",
                                   name + "_BINARY_DIR",
                                   name + "_ADDED",
                                   "CPM_LAST_PACKAGE_NAME",
                                   "CPM_PACKAGE_" + name + "_SOURCE_DIR",
                                   "CPM_PACKAGE_" + name + "_BINARY_DIR"};
    auto snippet = "block(SCOPE_FOR VARIABLES PROPAGATE "
                   + eacp::Strings::join(propagated, " ") + ")\n" + body
                   + "endblock()\n";

    for (const auto& variable: propagated)
        snippet += "set(" + variable + " \"${" + variable + "}\" PARENT_SCOPE)\n";

    return snippet;
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

SessionOverride parseOverride(const std::string& definition)
{
    auto equals = definition.find('=');
    auto variable = definition.substr(0, equals);

    for (auto prefix: overridePrefixes)
    {
        if (equals == std::string::npos
            || variable.size() <= prefix.size() + overrideSuffix.size()
            || !variable.starts_with(prefix) || !variable.ends_with(overrideSuffix))
            continue;

        auto nameLength = variable.size() - prefix.size() - overrideSuffix.size();
        return SessionOverride {variable.substr(prefix.size(), nameLength),
                                definition.substr(equals + 1),
                                variable};
    }

    throw std::runtime_error("--override takes LOCKSTEP_<Name>_SOURCE=<dir> or "
                             "CPM_<Name>_SOURCE=<dir>, not "
                             + definition);
}
} // namespace

namespace
{
PackageSpec parseArgumentsFrom(const CommandOptions& options)
{
    try
    {
        return parsePackageArguments(options.packageArguments);
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

    if (options.mode != updateMode && !isLockFileCurrent(options.lock))
    {
        printStatus(formatUserError(
            options.lock.str()
                + " is not in the form this lockstep writes: it is from another "
                  "version or was edited by hand. Rewrite it with:",
            "cmake -B <build> -DLOCKSTEP_MODE=update"));
    }

    auto session = Session {};

    for (const auto& definition: options.overrides)
    {
        auto entry = parseOverride(definition);

        if (!entry.sourceDirectory.empty())
            session.overrides.push_back(entry);
    }

    std::stable_partition(
        session.overrides.begin(),
        session.overrides.end(),
        [](const auto& entry)
        { return entry.variable.starts_with(overridePrefixes[0]); });

    writeSession(options.buildDirectory, session);
    return 0;
}

int runAddCommand(const CommandOptions& options)
{
    auto spec = parseArgumentsFrom(options);
    writeSnippet(options, addPackage(options, spec));
    return 0;
}

int runProvideCommand(const CommandOptions& options)
{
    auto spec = parseArgumentsFrom(options);

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
        {
            auto lockedNames = eacp::Vector<std::string> {};

            for (const auto& package: lock.packages)
                lockedNames.add(package.name);

            throw std::runtime_error(describeUnknownUpdateName(
                {name, getPathForUser(options, options.lock), lockedNames}));
        }
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

        printStatus(
            "FAILED " + package.name + ": "
            + describeDamagedCacheEntry(
                {package.name, cache.getSourceDirectory(package.hash).str()}));
        ++failedCount;
    }

    if (!isLockFileCurrent(options.lock))
    {
        printStatus(formatUserError(options.lock.str()
                                        + " is not in the form this lockstep "
                                          "writes. Rewrite it with:",
                                    "lockstep update"));
        ++failedCount;
    }

    if (lock.root != lock.computeRootHash())
    {
        printStatus(formatUserError("the root hash in " + options.lock.str()
                                        + " does not match its packages, so it was "
                                          "edited by hand. Rewrite it with:",
                                    "lockstep update"));
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
