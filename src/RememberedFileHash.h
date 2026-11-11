#pragma once

#include <Miro/Reflect.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lockstep
{
// A file's SHA-256 as of its size and modification time, so it is hashed again
// only once either changes.
struct RememberedFileHash
{
    std::string path;
    std::uint64_t size = 0;
    std::int64_t modificationTime = 0;
    std::string sha256;

    MIRO_REFLECT(path, size, modificationTime, sha256)
};

using RememberedFileHashes = std::vector<RememberedFileHash>;
} // namespace lockstep
