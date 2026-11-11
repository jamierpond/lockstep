#pragma once

#include "LibraryStore.h"

#include <chrono>
#include <cstdint>
#include <filesystem>

namespace lockstep
{
// How long after its last use an entry cannot be evicted. A configure marks
// every entry it hits and the build it configured copies them later, so this
// is how long a build has from its configure to the links that copy them.
constexpr auto evictionProtection = std::chrono::hours {1};

// The size limit of a store and the time it is checked at.
struct EvictionLimit
{
    std::uint64_t maximumBytes = 0;
    std::filesystem::file_time_type now =
        std::filesystem::file_time_type::clock::now();
};

struct EvictionResult
{
    int evictedCount = 0;

    // What the entries take after the evictions.
    std::uint64_t bytesInUse = 0;
};

// When the store's entries take more than the limit, deletes the least
// recently used until they fit, never one used within evictionProtection of
// `now`, so the store can stay over the limit. Each goes whole: it is moved out
// of its place before it is deleted. Another process evicting at the same
// time takes some of them instead, and neither fails.
EvictionResult evictLeastRecentlyUsed(const LibraryStore& store,
                                      const EvictionLimit& limit);
} // namespace lockstep
