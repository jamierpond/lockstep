#include "LibraryStore.h"
#include "LibraryCache.h"
#include "LongPath.h"
#include "Sha256.h"

#include <eacp/Core/Utils/File.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <emberstore/Database.h>

#include <Miro/Json.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <random>
#include <stdexcept>
#include <thread>

namespace lockstep
{
namespace
{
constexpr auto manifestName = "manifest";
constexpr auto statisticsName = "statistics";
constexpr auto usedMarkersName = "used";
constexpr auto publishAttempts = 3;
constexpr auto manifestIndent = 2;

using CountsMember = LibraryCacheCounts LibraryCacheStatistics::*;

constexpr auto countsOfEachKind =
    std::array<std::pair<CachedTargetKind, CountsMember>, 3> {
        {{CachedTargetKind::staticLibrary, &LibraryCacheStatistics::staticLibraries},
         {CachedTargetKind::executable, &LibraryCacheStatistics::executables},
         {CachedTargetKind::objectLibrary,
          &LibraryCacheStatistics::objectLibraries}}};

// A lost cache file only costs a rebuild, so no write waits for the disk.
constexpr auto storeDurability = emberstore::Durability::Atomic;

// emberstore gives up on its lock after 250 ms, and eacp's lock is not fair, so
// many links storing at once can starve one of them for longer than that.
constexpr auto statisticsUpdatePatience = std::chrono::seconds {30};

// A random pause between attempts, up to eacp's lock retry interval, so two
// processes that keep missing each other's free moments stop retrying in step.
constexpr auto longestPauseBetweenAttemptsInMilliseconds = 25;

void pauseForRandomMoment()
{
    thread_local auto generator = std::minstd_rand {std::random_device {}()};
    auto milliseconds =
        std::uniform_int_distribution {0, longestPauseBetweenAttemptsInMilliseconds};
    std::this_thread::sleep_for(std::chrono::milliseconds {milliseconds(generator)});
}

bool isNamedLikeKey(std::string_view name)
{
    auto isHexDigit = [](char character)
    { return std::isxdigit(static_cast<unsigned char>(character)) != 0; };

    return name.size() == libraryKeyLength && std::ranges::all_of(name, isHexDigit);
}

bool isNamedLikeLibraryKey(const eacp::FilePath& entry)
{
    return isNamedLikeKey(entry.fileName());
}

eacp::Vector<eacp::FilePath> listKeyNamedDirectories(const eacp::FilePath& directory)
{
    auto directories = eacp::Vector<eacp::FilePath> {};

    for (const auto& entry: eacp::Files::listDirectory(directory, {}))
    {
        if (entry.kind == eacp::Files::EntryKind::directory
            && isNamedLikeLibraryKey(entry.path))
            directories.add(entry.path);
    }

    return directories;
}

// Written into the staging directory with emberstore's atomic write rather
// than a Document, whose inter-process lock would leave a lock file named
// after every staging directory in eacp's lock directory.
void writeManifest(const eacp::FilePath& entryDirectory,
                   const LibraryManifest& manifest)
{
    auto path = getLibraryManifestPath(entryDirectory);

    if (!emberstore::writeFileAtomically(
            path, Miro::toJSONString(manifest, manifestIndent), storeDurability))
        throw std::runtime_error("cannot write " + path.str());
}

std::string describeDeleteFailure(const eacp::FilePath& path,
                                  const std::error_code& error)
{
    return "cannot delete " + path.str() + ": " + error.message();
}

// Moves the entry out of its key's place before deleting it, so an entry that
// cannot be moved, because a file in it is open on Windows, stays whole and
// usable rather than half deleted.
std::optional<std::string> deleteEntry(const eacp::FilePath& entry)
{
    auto retired = eacp::TemporaryFile {entry.parentDirectory(), ".deleting"};

    if (auto error = renameAtAnyLengthAndGetError(entry, retired.path()))
        return describeDeleteFailure(entry, error);

    if (auto error = removeTreeAtAnyLengthAndGetError(retired.path()))
        return describeDeleteFailure(retired.path(), error);

    return std::nullopt;
}

// Each key's directory, with every build of it inside.
eacp::Vector<eacp::FilePath>
    listEveryFormatEntry(const eacp::FilePath& everyFormatRoot)
{
    auto entries = eacp::Vector<eacp::FilePath> {};

    for (const auto& format: eacp::Files::listDirectory(everyFormatRoot, {}))
    {
        if (format.kind == eacp::Files::EntryKind::directory)
            entries.addFrom(listKeyNamedDirectories(format.path));
    }

    return entries;
}

eacp::Vector<std::string> deleteEveryEntry(const eacp::FilePath& everyFormatRoot)
{
    auto failures = eacp::Vector<std::string> {};

    for (const auto& entry: listEveryFormatEntry(everyFormatRoot))
    {
        if (auto failure = deleteEntry(entry))
            failures.add(*failure);
    }

    return failures;
}

eacp::Vector<std::string> deleteTree(const eacp::FilePath& path)
{
    if (auto error = removeTreeAtAnyLengthAndGetError(path))
        return {describeDeleteFailure(path, error)};

    return {};
}

CountsMember findCountsMember(CachedTargetKind kind)
{
    for (auto [entryKind, counts]: countsOfEachKind)
    {
        if (entryKind == kind)
            return counts;
    }

    throw std::logic_error("the library cache counts no such kind of entry");
}

// Renames the staged entry into place. An eviction can delete the key's
// directory once its last build is gone, between this making it and the
// rename, so a rename that finds no directory makes it again and retries.
bool publishStagedEntry(const eacp::FilePath& staging,
                        const eacp::FilePath& destination)
{
    for (auto attempt = 0; attempt < publishAttempts; ++attempt)
    {
        eacp::Files::createDirectories(destination.parentDirectory());

        if (renameAtAnyLength(staging, destination))
            return true;

        if (eacp::Files::isDirectory(destination.parentDirectory()))
            return false;
    }

    return false;
}

// Object files of different sources can share a file name, so each is stored
// under its place in the list.
std::string getStoredFileName(CachedTargetKind kind,
                              std::size_t index,
                              const eacp::FilePath& file)
{
    if (kind == CachedTargetKind::objectLibrary)
        return std::to_string(index) + "-" + file.fileName();

    return file.fileName();
}

emberstore::Document<LibraryCacheStatistics>
    openStatistics(const eacp::FilePath& storeRoot)
{
    return emberstore::Database {storeRoot, storeDurability}
        .document<LibraryCacheStatistics>(statisticsName);
}
} // namespace

std::optional<EntryAddress> parseEntryAddress(std::string_view text)
{
    auto slash = text.find('/');

    if (slash == std::string_view::npos)
        return std::nullopt;

    auto key = text.substr(0, slash);
    auto buildId = text.substr(slash + 1);

    if (!isNamedLikeKey(key) || !isNamedLikeKey(buildId))
        return std::nullopt;

    return EntryAddress {std::string {key}, std::string {buildId}};
}

eacp::FilePath getLibraryManifestPath(const eacp::FilePath& entryDirectory)
{
    return entryDirectory / (std::string {manifestName} + ".json");
}

std::optional<LibraryManifest>
    readLibraryManifest(const eacp::FilePath& entryDirectory)
{
    auto manifest = emberstore::Database {entryDirectory}
                        .document<LibraryManifest>(manifestName)
                        .read();

    if (manifest.files.empty())
        return std::nullopt;

    return manifest;
}

std::vector<eacp::FilePath> StoredEntry::getFilePaths() const
{
    auto paths = std::vector<eacp::FilePath> {};

    for (const auto& file: manifest.files)
        paths.push_back(directory / file.name);

    return paths;
}

LibraryCacheCounts& LibraryCacheStatistics::getCounts(CachedTargetKind kind)
{
    return this->*findCountsMember(kind);
}

const LibraryCacheCounts&
    LibraryCacheStatistics::getCounts(CachedTargetKind kind) const
{
    return this->*findCountsMember(kind);
}

void addStatistics(LibraryCacheStatistics& total,
                   const LibraryCacheStatistics& added)
{
    for (auto kind: getEveryCachedTargetKind())
    {
        auto& counts = total.getCounts(kind);
        const auto& addedCounts = added.getCounts(kind);
        counts.hits += addedCounts.hits;
        counts.misses += addedCounts.misses;
        counts.stores += addedCounts.stores;
        counts.fetchedHits += addedCounts.fetchedHits;
        counts.otherToolchainHits += addedCounts.otherToolchainHits;
    }

    total.evictions += added.evictions;
    total.locationEvictions += added.locationEvictions;
}

const std::vector<CachedTargetKind>& getEveryCachedTargetKind()
{
    static const auto kinds = []
    {
        auto everyKind = std::vector<CachedTargetKind> {};

        for (auto [kind, counts]: countsOfEachKind)
            everyKind.push_back(kind);

        return everyKind;
    }();

    return kinds;
}

std::string getLibraryFormatDirectoryName()
{
    return "format-" + std::to_string(libraryCacheFormatVersion);
}

LibraryStore::LibraryStore(const eacp::FilePath& cacheRoot)
    : LibraryStore(LibraryStoreDirectory {cacheRoot / "lib"})
{
}

LibraryStore::LibraryStore(const LibraryStoreDirectory& storeDirectory)
    : everyFormatRoot(storeDirectory.path)
    , root(everyFormatRoot / getLibraryFormatDirectoryName())
{
}

eacp::FilePath LibraryStore::getEntryDirectory(const EntryAddress& address) const
{
    return root / address.key / address.buildId;
}

std::optional<StoredEntry> LibraryStore::findEntry(const EntryAddress& address) const
{
    auto entry = StoredEntry {address, getEntryDirectory(address), {}};
    auto manifest = readLibraryManifest(entry.directory);

    if (!manifest)
        return std::nullopt;

    entry.manifest = *manifest;

    if (!std::ranges::all_of(entry.getFilePaths(), eacp::Files::isFile))
        return std::nullopt;

    return entry;
}

std::vector<StoredEntry> LibraryStore::listBuilds(const std::string& key) const
{
    auto builds = std::vector<StoredEntry> {};

    for (const auto& build: listKeyNamedDirectories(root / key))
    {
        if (auto entry = findEntry({key, build.fileName()}))
            builds.push_back(*entry);
    }

    return builds;
}

bool LibraryStore::storeEntry(const std::string& key,
                              const EntryToStore& entry) const
{
    auto address = EntryAddress {key, computeBuildId(entry.builtWith)};
    auto destination = getEntryDirectory(address);

    if (eacp::Files::isDirectory(destination))
        return false;

    eacp::Files::createDirectories(root);

    // A random name beside the entries, so two builds storing one entry never
    // write into the same directory and the rename publishes it whole.
    auto staging = eacp::TemporaryFile {root, ".staging"};
    auto manifest = LibraryManifest {entry.kind, {}, entry.inputs, entry.builtWith};

    for (auto index = std::size_t {0}; index < entry.files.size(); ++index)
    {
        const auto& file = entry.files[index];
        auto name = getStoredFileName(entry.kind, index, file);
        auto stagedFile = staging.path() / name;

        if (!eacp::Files::copyFile(file, stagedFile))
            throw std::runtime_error("cannot copy " + file.str() + " into "
                                     + staging.path().str());

        manifest.files.push_back({name, computeFileSha256Hex(stagedFile)});
    }

    writeManifest(staging.path(), manifest);

    if (publishStagedEntry(staging.path(), destination))
    {
        markUsed(address);
        return true;
    }

    if (!eacp::Files::isDirectory(destination))
        throw std::runtime_error("cannot move the stored entry into "
                                 + destination.str());

    markUsed(address);
    return false;
}

void LibraryStore::markUsed(const EntryAddress& address) const
{
    auto marker = getUsedMarkerPath(address);
    auto error = std::error_code {};
    auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(eacp::toStdPath(marker), now, error);

    if (!error)
        return;

    eacp::Files::createDirectories(marker.parentDirectory());
    eacp::Files::writeFile(marker, std::string_view {});
}

std::filesystem::file_time_type
    LibraryStore::findLastUse(const EntryAddress& address) const
{
    auto error = std::error_code {};
    auto time = std::filesystem::last_write_time(
        eacp::toStdPath(getUsedMarkerPath(address)), error);

    if (!error)
        return time;

    auto manifest = getLibraryManifestPath(getEntryDirectory(address));
    time = std::filesystem::last_write_time(eacp::toStdPath(manifest), error);

    if (!error)
        return time;

    return std::filesystem::file_time_type::min();
}

std::vector<EntryAddress> LibraryStore::listEveryBuild() const
{
    auto addresses = std::vector<EntryAddress> {};

    for (const auto& key: listKeyNamedDirectories(root))
    {
        for (const auto& build: listKeyNamedDirectories(key))
            addresses.push_back({key.fileName(), build.fileName()});
    }

    return addresses;
}

std::uint64_t LibraryStore::measureEntry(const EntryAddress& address) const
{
    auto options = eacp::Files::DirectoryOptions {};
    options.recursive = true;
    options.includeHidden = true;
    auto bytes = std::uint64_t {0};

    for (const auto& file:
         eacp::Files::listFiles(getEntryDirectory(address), options))
        bytes += eacp::File {file}.size();

    return bytes;
}

bool LibraryStore::evictEntry(const EntryAddress& address) const
{
    auto directory = getEntryDirectory(address);

    if (!eacp::Files::isDirectory(directory) || deleteEntry(directory))
        return false;

    eacp::Files::removeFile(getUsedMarkerPath(address));
    auto error = std::error_code {};
    std::filesystem::remove(eacp::toStdPath(directory.parentDirectory()), error);
    return true;
}

eacp::FilePath LibraryStore::getUsedMarkerPath(const EntryAddress& address) const
{
    return root / usedMarkersName / (address.key + "." + address.buildId);
}

void LibraryStore::addToStatistics(const LibraryCacheStatistics& added) const
{
    auto addCounts = [&](LibraryCacheStatistics& total)
    { addStatistics(total, added); };

    auto statistics = openStatistics(root);
    auto deadline = std::chrono::steady_clock::now() + statisticsUpdatePatience;

    while (!statistics.mutate(addCounts))
    {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error(
                "cannot update " + statistics.filePath().str() + " within "
                + std::to_string(statisticsUpdatePatience.count()) + " seconds");

        pauseForRandomMoment();
    }
}

LibraryStoreSummary LibraryStore::summarize() const
{
    auto summary = LibraryStoreSummary {};

    if (!eacp::Files::isDirectory(root))
        return summary;

    for (const auto& key: listKeyNamedDirectories(root))
    {
        for (const auto& stored: listBuilds(key.fileName()))
            ++summary.entryCounts[stored.manifest.kind];
    }

    auto options = eacp::Files::DirectoryOptions {};
    options.recursive = true;
    options.includeHidden = true;

    for (const auto& file: eacp::Files::listFiles(root, options))
        summary.byteCount += eacp::File {file}.size();

    summary.statistics = openStatistics(root).read();
    return summary;
}

void LibraryStore::clear() const
{
    if (!eacp::Files::isDirectory(everyFormatRoot))
        return;

    auto failures = deleteEveryEntry(everyFormatRoot);

    if (failures.empty())
        failures = deleteTree(everyFormatRoot);

    if (!failures.empty())
        throw std::runtime_error(eacp::Strings::join(failures, "\n"));
}
} // namespace lockstep
