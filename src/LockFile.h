#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Reflect.h>

#include <optional>
#include <vector>
#include <string>

namespace lockstep
{
constexpr auto lockFileFormatVersion = 1;

struct LockedPackage
{
    std::string name;
    std::string kind;
    std::string url;
    std::string requestedRef;
    std::vector<std::string> patches;
    std::string commit;
    std::string hash;

    MIRO_REFLECT(name, kind, url, requestedRef, patches, commit, hash)
};

struct LockFile
{
    int version = lockFileFormatVersion;
    std::string root;
    std::string bootstrap;
    std::vector<LockedPackage> packages;

    MIRO_REFLECT(version, root, bootstrap, packages)

    std::optional<LockedPackage> findPackage(const std::string& name) const;
    void storePackage(const LockedPackage& package);
    std::string computeRootHash() const;
};

// An empty lock when the file does not exist yet.
LockFile readLockFile(const eacp::FilePath& path);

// Sorts the packages by name and recomputes the root hash before writing, so
// the file only changes when a package does.
void writeLockFile(const eacp::FilePath& path, LockFile lock);
} // namespace lockstep
