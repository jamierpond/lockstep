#pragma once

#include "LibraryEviction.h"
#include "LibraryStore.h"

#include <eacp/Core/Utils/FilePath.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace lockstep
{
enum class LibraryCacheLocationKind
{
    directory,
    ssh
};

// Where LOCKSTEP_LIBRARY_CACHE_LOCATION puts the library cache: a directory, or
// `[user@]host:directory` on a Unix host reached over ssh.
struct LibraryCacheLocation
{
    LibraryCacheLocationKind kind = LibraryCacheLocationKind::directory;

    // `[user@]host` for ssh; empty for a directory.
    std::string sshHost;
    std::string directory;

    // The setting as the user wrote it, for messages.
    std::string text;
};

// nullopt when `text` is empty. `C:\dir` and `C:/dir` are directories, not a
// host named C. Throws for an ssh directory holding a single quote.
std::optional<LibraryCacheLocation>
    parseLibraryCacheLocation(const std::string& text);

struct LibraryFetchRequest
{
    // This machine's own builds of the keys it has no copy of: fetched when the
    // location holds them.
    std::vector<EntryAddress> missing;

    // When a range is set: for a missing build the location lacks, every
    // build it holds of that key, for this machine to choose from.
    bool fetchesOtherBuilds = false;

    // Builds this machine has a copy of: the result names those the location
    // lacks.
    std::vector<EntryAddress> copied;
};

struct DiscardedLibrary
{
    EntryAddress address;
    std::string reason;
};

struct LibraryFetchResult
{
    std::set<EntryAddress> fetched;
    std::vector<DiscardedLibrary> discarded;
    std::vector<EntryAddress> locationLacks;
};

class LibraryCacheTransport;

// ssh for a location over ssh, unless a caller names another program that takes
// ssh's arguments.
constexpr auto defaultSshExecutable = "ssh";

// The library cache at a location, which lockstep copies libraries from and to.
// The linker needs a file on this disk, so every library fetched from it is
// copied into the local copies in <cache>/lib first, by both kinds alike.
class LibraryCacheAtLocation
{
public:
    LibraryCacheAtLocation(const LibraryCacheLocation& location,
                           const eacp::FilePath& cmake,
                           const std::string& sshExecutable = defaultSshExecutable);

    // One round trip: marks every build it asks about that the location holds
    // used there, copies every missing build the location holds (and,
    // when the request says so, the other builds of a missing key) into
    // `localCopies`, after checking each against its manifest, and learns
    // which copied builds the location lacks. Throws when the location cannot
    // be reached.
    LibraryFetchResult fetchLibraries(const LibraryFetchRequest& request,
                                      const LibraryStore& localCopies) const;

    // Copies the builds from `localCopies` to the location, each published
    // whole under its key and marked used, then evicts the least recently used
    // there while the location takes more than `locationLimit` (see
    // evictLeastRecentlyUsed). Returns how many it evicted. Throws when the
    // location cannot be reached.
    int sendLibraries(const std::vector<EntryAddress>& addresses,
                      const LibraryStore& localCopies,
                      const EvictionLimit& locationLimit) const;

    const LibraryCacheLocation& getLocation() const { return location; }

private:
    LibraryCacheLocation location;
    std::shared_ptr<const LibraryCacheTransport> transport;
};
} // namespace lockstep
