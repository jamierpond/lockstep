#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <string>

namespace lockstep
{
struct PinnedRelease
{
    std::string version;
    std::string sha256;
    std::string releaseUrl;
};

// "macos-arm64", "windows-x64", ...: the name release assets carry.
std::string getHostPlatformName();

// The lockstep.lock in `directory` or the nearest folder above it.
std::optional<eacp::FilePath> findEnclosingLockFile(const eacp::FilePath& directory);

// The version and this host's binary hash that a shim pins, read from its
// set(LOCKSTEP_VERSION ...) and set(LOCKSTEP_SHA256_<platform> ...) lines.
std::optional<PinnedRelease> readPinnedRelease(const eacp::FilePath& shim);

// Downloads the pinned binary into <cache>/bin/<version>/ unless it is already
// there, checks its SHA-256 and returns its path.
eacp::FilePath fetchPinnedExecutable(const PinnedRelease& release,
                                     const eacp::FilePath& cache);
} // namespace lockstep
