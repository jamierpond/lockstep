#pragma once

#include "PackageKind.h"
#include "Submodules.h"

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Reflect.h>

#include <optional>
#include <vector>
#include <string>

namespace lockstep
{
constexpr auto lockFileFormatVersion = 1;
constexpr auto lockFileName = "lockstep.lock";

struct LockedPackage
{
    std::string name;
    PackageKind kind = PackageKind::git;
    std::string url;
    std::string requestedRef;
    std::vector<std::string> patches;
    SubmoduleSelection submoduleSelection;
    std::string commit;
    std::vector<LockedSubmodule> submodules;
    bool usesGitLfs = false;
    std::string hash;

    MIRO_REFLECT(name,
                 kind,
                 url,
                 requestedRef,
                 patches,
                 submoduleSelection,
                 commit,
                 submodules,
                 usesGitLfs,
                 hash)
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

// Whether the file holds exactly what this lockstep would write for it, so a
// lock from an older version, missing fields this one records, is noticed.
// Line ends do not count: a CRLF checkout of the same lock is current, and is
// left as it is rather than rewritten, since git would show the rewrite as a
// change on a machine whose checkout converts line ends.
bool isLockFileCurrent(const eacp::FilePath& path);

// Sorts the packages by name and recomputes the root hash before writing, so
// the file only changes when a package does.
void writeLockFile(const eacp::FilePath& path, LockFile lock);
} // namespace lockstep
