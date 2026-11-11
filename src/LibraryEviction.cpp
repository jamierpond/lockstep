#include "LibraryEviction.h"

#include <algorithm>

namespace lockstep
{
namespace
{
struct EntryUse
{
    EntryAddress address;
    std::filesystem::file_time_type lastUse;
    std::uint64_t bytes = 0;
};

std::vector<EntryUse> listEntryUses(const LibraryStore& store)
{
    auto uses = std::vector<EntryUse> {};

    for (const auto& address: store.listEveryBuild())
        uses.push_back(
            {address, store.findLastUse(address), store.measureEntry(address)});

    std::ranges::sort(uses, {}, &EntryUse::lastUse);
    return uses;
}

std::uint64_t addBytes(const std::vector<EntryUse>& uses)
{
    auto bytes = std::uint64_t {0};

    for (const auto& use: uses)
        bytes += use.bytes;

    return bytes;
}
} // namespace

EvictionResult evictLeastRecentlyUsed(const LibraryStore& store,
                                      const EvictionLimit& limit)
{
    auto uses = listEntryUses(store);
    auto result = EvictionResult {0, addBytes(uses)};
    auto protectedSince = limit.now - evictionProtection;

    for (const auto& use: uses)
    {
        if (result.bytesInUse <= limit.maximumBytes || use.lastUse > protectedSince)
            break;

        if (store.evictEntry(use.address))
            ++result.evictedCount;
        else if (store.findEntry(use.address))
            continue;

        result.bytesInUse -= use.bytes;
    }

    return result;
}
} // namespace lockstep
