#pragma once

#include "LibraryStore.h"

#include <eacp/Core/Utils/FilePath.h>

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
    // Keys this machine has no copy of: fetched when the location holds them.
    std::vector<std::string> missingKeys;

    // Keys this machine has a copy of: the result names those the location lacks.
    std::vector<std::string> copiedKeys;
};

struct DiscardedLibrary
{
    std::string key;
    std::string reason;
};

struct LibraryFetchResult
{
    std::set<std::string> fetchedKeys;
    std::vector<DiscardedLibrary> discarded;
    std::vector<std::string> keysLocationLacks;
};

class LibraryCacheTransport;

// The library cache at a location, which lockstep copies libraries from and to.
// The linker needs a file on this disk, so every library fetched from it is
// copied into the local copies in <cache>/lib first, by both kinds alike.
class LibraryCacheAtLocation
{
public:
    LibraryCacheAtLocation(const LibraryCacheLocation& location,
                           const eacp::FilePath& cmake);

    // One round trip: copies every missing key the location holds into
    // `localCopies`, after checking it against its manifest, and learns which
    // copied keys the location lacks. Throws when the location cannot be reached.
    LibraryFetchResult fetchLibraries(const LibraryFetchRequest& request,
                                      const LibraryStore& localCopies) const;

    // Copies the entries of `keys` from `localCopies` to the location, each
    // published whole under its key. Throws when the location cannot be reached.
    void sendLibraries(const std::vector<std::string>& keys,
                       const LibraryStore& localCopies) const;

    const LibraryCacheLocation& getLocation() const { return location; }

private:
    LibraryCacheLocation location;
    std::shared_ptr<const LibraryCacheTransport> transport;
};
} // namespace lockstep
