#include "LibraryStore.h"
#include "LibraryCache.h"
#include "LongPath.h"
#include "Sha256.h"

#include <eacp/Core/Utils/File.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <emberstore/Database.h>

#include <Miro/Json.h>

#include <algorithm>
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
constexpr auto manifestIndent = 2;

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

bool isNamedLikeLibraryKey(const eacp::FilePath& entry)
{
    auto name = entry.fileName();
    auto isHexDigit = [](char character)
    { return std::isxdigit(static_cast<unsigned char>(character)) != 0; };

    return name.size() == libraryKeyLength && std::ranges::all_of(name, isHexDigit);
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

emberstore::Document<LibraryCacheStatistics>
    openStatistics(const eacp::FilePath& storeRoot)
{
    return emberstore::Database {storeRoot, storeDurability}
        .document<LibraryCacheStatistics>(statisticsName);
}
} // namespace

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

    if (manifest.fileName.empty())
        return std::nullopt;

    return manifest;
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

eacp::FilePath LibraryStore::getEntryDirectory(const std::string& key) const
{
    return root / key;
}

std::optional<LibraryManifest>
    LibraryStore::readManifest(const std::string& key) const
{
    return readLibraryManifest(getEntryDirectory(key));
}

std::optional<eacp::FilePath> LibraryStore::findLibrary(const std::string& key) const
{
    auto entryDirectory = getEntryDirectory(key);
    auto manifest = readLibraryManifest(entryDirectory);

    if (!manifest)
        return std::nullopt;

    auto library = entryDirectory / manifest->fileName;

    if (!eacp::Files::isFile(library))
        return std::nullopt;

    return library;
}

bool LibraryStore::storeLibrary(const std::string& key,
                                const eacp::FilePath& library,
                                const std::string& inputs) const
{
    auto destination = getEntryDirectory(key);

    if (eacp::Files::isDirectory(destination))
        return false;

    eacp::Files::createDirectories(root);

    // A random name beside the entry, so two builds storing one key never
    // write into the same directory and the rename publishes it whole.
    auto staging = eacp::TemporaryFile {root, ".staging"};
    auto stagedLibrary = staging.path() / library.fileName();

    if (!eacp::Files::copyFile(library, stagedLibrary))
        throw std::runtime_error("cannot copy " + library.str() + " into "
                                 + staging.path().str());

    auto manifest = LibraryManifest {
        library.fileName(), inputs, computeFileSha256Hex(stagedLibrary)};

    writeManifest(staging.path(), manifest);

    if (renameAtAnyLength(staging.path(), destination))
        return true;

    if (!eacp::Files::isDirectory(destination))
        throw std::runtime_error("cannot move the stored library into "
                                 + destination.str());

    return false;
}

void LibraryStore::addToStatistics(const LibraryCacheStatistics& added) const
{
    auto addCounts = [&](LibraryCacheStatistics& total)
    {
        total.hits += added.hits;
        total.misses += added.misses;
        total.stores += added.stores;
        total.fetchedHits += added.fetchedHits;
    };

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

    for (const auto& entry: eacp::Files::listDirectory(root, {}))
    {
        if (isNamedLikeLibraryKey(entry.path) && findLibrary(entry.path.fileName()))
            ++summary.libraryCount;
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
    if (eacp::Files::isDirectory(everyFormatRoot)
        && !eacp::Files::removeAll(everyFormatRoot))
        throw std::runtime_error("cannot delete " + everyFormatRoot.str());
}
} // namespace lockstep
