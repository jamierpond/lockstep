#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Reflect.h>

#include <cstdint>
#include <optional>
#include <string>

namespace lockstep
{
// A key is the hex SHA-256 of the key inputs.
constexpr auto libraryKeyLength = std::size_t {64};

struct LibraryManifest
{
    std::string fileName;

    // The text the key is the SHA-256 of, so a miss can be compared with a
    // stored entry by eye.
    std::string inputs;

    // The hex SHA-256 of the library file, which a download is checked against.
    std::string librarySha256;

    MIRO_REFLECT(fileName, inputs, librarySha256)
};

// Added up since the last `cache clear`: hits and misses by every configure,
// stores by every link that stored a library.
struct LibraryCacheStatistics
{
    int hits = 0;
    int misses = 0;
    int stores = 0;

    // The hits whose library a configure fetched from the library cache
    // location, because this machine had no copy of it.
    int fetchedHits = 0;

    MIRO_REFLECT(hits, misses, stores, fetchedHits)
};

struct LibraryStoreSummary
{
    int libraryCount = 0;
    std::uint64_t byteCount = 0;
    LibraryCacheStatistics statistics;
};

// The manifest.json in an entry directory, or nullopt when it is missing or
// unreadable.
std::optional<LibraryManifest>
    readLibraryManifest(const eacp::FilePath& entryDirectory);

eacp::FilePath getLibraryManifestPath(const eacp::FilePath& entryDirectory);

// format-<version>, the directory of a store's entries in the current format.
std::string getLibraryFormatDirectoryName();

// The directory that holds a store's format-<version> directories: <cache>/lib
// for the local copies, or a library cache location that is a directory.
struct LibraryStoreDirectory
{
    eacp::FilePath path;
};

// Built static libraries under <cache>/lib/format-<version>/<key>/: the library
// file and manifest.json. An entry appears whole or not at all. Entries of an
// older format sit in another directory, so nothing here reads them.
class LibraryStore
{
public:
    explicit LibraryStore(const eacp::FilePath& cacheRoot);
    explicit LibraryStore(const LibraryStoreDirectory& storeDirectory);

    std::optional<eacp::FilePath> findLibrary(const std::string& key) const;

    std::optional<LibraryManifest> readManifest(const std::string& key) const;

    // <store directory>/format-<version>, which holds one directory per key.
    const eacp::FilePath& getFormatDirectory() const { return root; }

    // Copies `library` in under `key`, unless an entry for `key` is there
    // already. True when this call stored it.
    bool storeLibrary(const std::string& key,
                      const eacp::FilePath& library,
                      const std::string& inputs) const;

    // Adds to the counts in statistics.json, which another lockstep may be
    // adding to at the same time. Keeps trying for 30 seconds, then throws.
    void addToStatistics(const LibraryCacheStatistics& added) const;

    LibraryStoreSummary summarize() const;

    // Deletes every stored library and the counts, of every format: the whole
    // of <cache>/lib. An entry it cannot delete stays whole; it then throws,
    // naming each path it could not delete and why, and leaves the counts.
    void clear() const;

private:
    eacp::FilePath getEntryDirectory(const std::string& key) const;

    eacp::FilePath everyFormatRoot;
    eacp::FilePath root;
};
} // namespace lockstep
