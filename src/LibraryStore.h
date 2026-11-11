#pragma once

#include "BuiltWith.h"

#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Reflect.h>

#include <compare>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lockstep
{
// A key is the hex SHA-256 of the key inputs.
constexpr auto libraryKeyLength = std::size_t {64};

// What a store entry holds: a static library or an executable is one file, an
// object library is its object files in the order $<TARGET_OBJECTS:...> lists
// them.
enum class CachedTargetKind
{
    staticLibrary,
    executable,
    objectLibrary
};

struct StoredFile
{
    std::string name;

    // The hex SHA-256 of the file, which a download is checked against.
    std::string sha256;

    MIRO_REFLECT(name, sha256)
};

struct LibraryManifest
{
    CachedTargetKind kind = CachedTargetKind::staticLibrary;
    std::vector<StoredFile> files;

    // The text the key is the SHA-256 of, so a miss can be compared with a
    // stored entry by eye.
    std::string inputs;

    // What built it; the build id is the SHA-256 of this record's JSON.
    BuiltWith builtWith;

    MIRO_REFLECT(kind, files, inputs, builtWith)
};

// Where one build of a key sits in a store: <key>/<build id>.
struct EntryAddress
{
    std::string key;
    std::string buildId;

    std::string getPath() const { return key + "/" + buildId; }

    auto operator<=>(const EntryAddress&) const = default;
};

// nullopt unless `text` is <key>/<build id>, both named like keys.
std::optional<EntryAddress> parseEntryAddress(std::string_view text);

// What one kind of entry counted since the last `cache clear`: hits and misses
// by every configure, stores by every link that stored one.
struct LibraryCacheCounts
{
    int hits = 0;
    int misses = 0;
    int stores = 0;

    // The hits whose entry a configure fetched from the library cache
    // location, because this machine had no copy of it.
    int fetchedHits = 0;

    // The hits on a build another toolchain made, which a range admitted.
    int otherToolchainHits = 0;

    MIRO_REFLECT(hits, misses, stores, fetchedHits, otherToolchainHits)
};

struct LibraryCacheStatistics
{
    LibraryCacheCounts staticLibraries;
    LibraryCacheCounts executables;
    LibraryCacheCounts objectLibraries;

    // Entries deleted to stay under the size limit: from this store, and from
    // the library cache location by this machine's sends.
    int evictions = 0;
    int locationEvictions = 0;

    LibraryCacheCounts& getCounts(CachedTargetKind kind);
    const LibraryCacheCounts& getCounts(CachedTargetKind kind) const;

    MIRO_REFLECT(
        staticLibraries, executables, objectLibraries, evictions, locationEvictions)
};

void addStatistics(LibraryCacheStatistics& total,
                   const LibraryCacheStatistics& added);

// Every kind, in the order the configure line and `cache stats` name them.
const std::vector<CachedTargetKind>& getEveryCachedTargetKind();

struct LibraryStoreSummary
{
    // How many entries of each kind are stored now.
    std::map<CachedTargetKind, int> entryCounts;
    std::uint64_t byteCount = 0;
    LibraryCacheStatistics statistics;
};

// The files to copy in as one entry, in the order the manifest lists them.
struct EntryToStore
{
    CachedTargetKind kind = CachedTargetKind::staticLibrary;
    std::vector<eacp::FilePath> files;
    std::string inputs;
    BuiltWith builtWith;
};

// An entry as the store holds it: where, its directory and its manifest.
struct StoredEntry
{
    EntryAddress address;
    eacp::FilePath directory;
    LibraryManifest manifest;

    std::vector<eacp::FilePath> getFilePaths() const;
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

// Built static libraries, executables and object files under
// <cache>/lib/format-<version>/<key>/<build id>/: the entry's files and
// manifest.json, one build per record of what built it. An entry appears whole
// or not at all. Entries of an older format sit in another directory, so
// nothing here reads them.
class LibraryStore
{
public:
    explicit LibraryStore(const eacp::FilePath& cacheRoot);
    explicit LibraryStore(const LibraryStoreDirectory& storeDirectory);

    // The entry, when its manifest and every file it names are there.
    std::optional<StoredEntry> findEntry(const EntryAddress& address) const;

    // Every whole build of `key`, in build id order.
    std::vector<StoredEntry> listBuilds(const std::string& key) const;

    // <store directory>/format-<version>, which holds one directory per key.
    const eacp::FilePath& getFormatDirectory() const { return root; }

    // Copies the files in under `key` and the id of the entry's record,
    // unless that build is there already, and marks it used. True when this
    // call stored it.
    bool storeEntry(const std::string& key, const EntryToStore& entry) const;

    // Sets the time the entry was last used to now, by touching its marker
    // <store>/format-<version>/used/<key>.<build id>, so no large file is
    // rewritten.
    void markUsed(const EntryAddress& address) const;

    // When the entry was last used: its marker's time, or its manifest's when
    // it has no marker, such as an entry an older lockstep stored.
    std::filesystem::file_time_type findLastUse(const EntryAddress& address) const;

    // Every build of every key, whole or not.
    std::vector<EntryAddress> listEveryBuild() const;

    // Bytes of every file in the entry's directory.
    std::uint64_t measureEntry(const EntryAddress& address) const;

    // Moves the entry out of its place, so no lookup finds part of it, then
    // deletes it and its marker. False when another process moved it first or
    // it cannot be moved.
    bool evictEntry(const EntryAddress& address) const;

    // Adds to the counts in statistics.json, which another lockstep may be
    // adding to at the same time. Keeps trying for 30 seconds, then throws.
    void addToStatistics(const LibraryCacheStatistics& added) const;

    LibraryStoreSummary summarize() const;

    // Deletes every stored entry and the counts, of every format: the whole
    // of <cache>/lib. An entry it cannot delete stays whole; it then throws,
    // naming each path it could not delete and why, and leaves the counts.
    void clear() const;

private:
    eacp::FilePath getEntryDirectory(const EntryAddress& address) const;
    eacp::FilePath getUsedMarkerPath(const EntryAddress& address) const;

    eacp::FilePath everyFormatRoot;
    eacp::FilePath root;
};
} // namespace lockstep
