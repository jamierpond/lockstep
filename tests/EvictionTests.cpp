#include "LibrarySender.h"
#include "StoreEvictor.h"
#include "TestDirectory.h"

#include <ByteSize.h>
#include <LibraryCacheLocation.h>
#include <LibraryEviction.h>
#include <LibraryStore.h>
#include <Sha256.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/StdPath.h>

#include <stdexcept>

using namespace nano;

namespace EvictionTests
{
constexpr auto testExecutablePath = LOCKSTEP_TESTS_EXECUTABLE;
constexpr auto entryText = std::string_view {"0123456789"};
constexpr auto oldEntryCount = 20;
constexpr auto entriesThatFit = 5;
constexpr auto manifestAllowance = std::uint64_t {4096};
constexpr auto kibibyte = std::uint64_t {1024};

using FileTime = std::filesystem::file_time_type;

FileTime getHoursAgo(int hours)
{
    return FileTime::clock::now() - std::chrono::hours {hours};
}

bool isRefusedAsSize(std::string_view text)
{
    try
    {
        lockstep::parseByteSize(text);
        return false;
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
}

struct SampleStore
{
    eacp::FilePath directory;
    lockstep::LibraryStore store;
    eacp::FilePath library;
};

SampleStore createSampleStore(const std::string& name,
                              const eacp::FilePath& storeDirectory)
{
    auto directory = createEmptyTestDirectory(name);
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, entryText);
    return {directory,
            lockstep::LibraryStore {
                lockstep::LibraryStoreDirectory {directory / storeDirectory.str()}},
            library};
}

eacp::FilePath getMarkerPath(const lockstep::LibraryStore& store,
                             const lockstep::EntryAddress& address)
{
    return store.getFormatDirectory() / "used"
           / (address.key + "." + address.buildId);
}

// Stores an entry whose last use was `hoursAgo` hours ago.
lockstep::EntryAddress
    storeUsedEntry(const SampleStore& sample, int index, int hoursAgo)
{
    auto inputs = "inputs " + std::to_string(index);
    auto key = lockstep::computeSha256Hex(inputs);
    sample.store.storeEntry(
        key,
        {lockstep::CachedTargetKind::staticLibrary, {sample.library}, inputs, {}});
    auto address = lockstep::EntryAddress {key, lockstep::computeBuildId({})};
    std::filesystem::last_write_time(
        eacp::toStdPath(getMarkerPath(sample.store, address)),
        getHoursAgo(hoursAgo));
    return address;
}

std::uint64_t measureEveryEntry(const lockstep::LibraryStore& store)
{
    auto bytes = std::uint64_t {0};

    for (const auto& address: store.listEveryBuild())
        bytes += store.measureEntry(address);

    return bytes;
}

bool hasLeftOvers(const eacp::FilePath& directory)
{
    auto options = eacp::Files::DirectoryOptions {};
    options.recursive = true;
    options.includeHidden = true;
    auto isLeftOver = [](const eacp::Files::DirectoryEntry& entry)
    {
        auto name = entry.path.fileName();
        return name.starts_with(".deleting") || name.starts_with(".evicting")
               || name.starts_with(".staging");
    };
    return std::ranges::any_of(eacp::Files::listDirectory(directory, options),
                               isLeftOver);
}

bool isEveryBuildWhole(const lockstep::LibraryStore& store)
{
    auto isWhole = [&](const lockstep::EntryAddress& address)
    { return store.findEntry(address).has_value(); };
    return std::ranges::all_of(store.listEveryBuild(), isWhole);
}
} // namespace EvictionTests

using namespace EvictionTests;

auto tParsesSizes = test("ByteSize/parsesSizesWithUnits") = []
{
    check(lockstep::parseByteSize("5 GiB") == 5ULL * kibibyte * kibibyte * kibibyte);
    check(lockstep::parseByteSize("5GiB") == lockstep::parseByteSize("5 gib"));
    check(lockstep::parseByteSize("500 MB") == 500'000'000ULL);
    check(lockstep::parseByteSize("1.5 KiB") == 1536);
    check(lockstep::parseByteSize("4096") == 4096);
    check(lockstep::parseByteSize(" 2 TB ") == 2'000'000'000'000ULL);
    check(lockstep::describeByteSize(lockstep::parseByteSize("5 GiB")) == "5.0 GiB");
    check(lockstep::describeByteSize(12) == "12 B");
};

auto tRefusesSizes = test("ByteSize/refusesWhatIsNotASize") = []
{
    for (auto text: {"",
                     "GiB",
                     "5 GB extra",
                     "-1 GiB",
                     "0",
                     "0 GiB",
                     "5 XB",
                     "five",
                     "1.2.3 MB",
                     "99999999999 TiB"})
        check(isRefusedAsSize(text));
};

auto tMarksUse = test("LibraryStore/marksAnEntryUsedWithoutRewritingIt") = []
{
    auto sample = createSampleStore("store-marks-use", eacp::FilePath {"store"});
    auto address = storeUsedEntry(sample, 0, 3);
    auto library = sample.store.findEntry(address)->getFilePaths().front();
    auto libraryTime = std::filesystem::last_write_time(eacp::toStdPath(library));

    check(sample.store.findLastUse(address) < getHoursAgo(2));
    sample.store.markUsed(address);
    check(sample.store.findLastUse(address) > getHoursAgo(1));
    check(std::filesystem::last_write_time(eacp::toStdPath(library)) == libraryTime);

    eacp::Files::removeFile(getMarkerPath(sample.store, address));
    check(sample.store.findLastUse(address) > getHoursAgo(1));
};

auto tEvictsLeastRecent =
    test("LibraryEviction/deletesTheLeastRecentlyUsedFirst") = []
{
    auto sample = createSampleStore("evicts-least-recent", eacp::FilePath {"store"});
    auto oldest = storeUsedEntry(sample, 0, 5);
    auto older = storeUsedEntry(sample, 1, 4);
    auto newest = storeUsedEntry(sample, 2, 3);
    auto oneEntry = sample.store.measureEntry(newest);

    auto result = lockstep::evictLeastRecentlyUsed(sample.store, {oneEntry});

    check(result.evictedCount == 2);
    check(result.bytesInUse == oneEntry);
    check(!sample.store.findEntry(oldest) && !sample.store.findEntry(older));
    check(sample.store.findEntry(newest).has_value());
    check(!eacp::Files::exists(getMarkerPath(sample.store, oldest)));
    check(!eacp::Files::isDirectory(sample.store.getFormatDirectory() / oldest.key));
    check(!hasLeftOvers(sample.directory));
};

auto tProtectsRecentUse =
    test("LibraryEviction/neverDeletesWhatWasUsedInTheLastHour") = []
{
    auto sample = createSampleStore("evicts-protected", eacp::FilePath {"store"});
    auto old = storeUsedEntry(sample, 0, 2);
    auto justHit = storeUsedEntry(sample, 1, 2);
    sample.store.markUsed(justHit);

    auto result = lockstep::evictLeastRecentlyUsed(sample.store, {1});

    check(result.evictedCount == 1);
    check(!sample.store.findEntry(old));
    check(sample.store.findEntry(justHit).has_value());
    check(result.bytesInUse > 1);
};

auto tUnderLimit = test("LibraryEviction/leavesAStoreUnderItsLimitAlone") = []
{
    auto sample = createSampleStore("evicts-nothing", eacp::FilePath {"store"});
    auto address = storeUsedEntry(sample, 0, 5);
    auto result = lockstep::evictLeastRecentlyUsed(sample.store, {unlimitedBytes});
    check(result.evictedCount == 0 && sample.store.findEntry(address).has_value());
};

auto tTwoEvictors = test("LibraryEviction/twoProcessesEvictingAtOnceEndWell") = []
{
    auto sample =
        createSampleStore("evicts-concurrently", eacp::FilePath {"cache/lib"});

    for (auto index = 0; index < oldEntryCount; ++index)
        storeUsedEntry(sample, index, oldEntryCount - index + 1);

    auto oneEntry = measureEveryEntry(sample.store) / oldEntryCount;
    auto limit = oneEntry * entriesThatFit;
    auto arguments = eacp::Vector<std::string> {std::string {storeEvictorArgument},
                                                (sample.directory / "cache").str(),
                                                std::to_string(limit)};
    auto first = eacp::Processes::Process {testExecutablePath, arguments};
    auto second = eacp::Processes::Process {testExecutablePath, arguments};

    check(first.launched() && second.launched());
    check(first.wait() == 0);
    check(second.wait() == 0);
    check(measureEveryEntry(sample.store) <= limit);
    check(!sample.store.listEveryBuild().empty());
    check(isEveryBuildWhole(sample.store));
    check(!hasLeftOvers(sample.directory));
};

#if !defined(_WIN32)
lockstep::LibraryCacheAtLocation
    createFakeSshLocation(const eacp::FilePath& directory)
{
    return {*lockstep::parseLibraryCacheLocation("lockstep-fake-host:"
                                                 + directory.str()),
            eacp::FilePath {"cmake"},
            testExecutablePath};
}

auto tHostEvicts =
    test("LocationOverSsh/aSendOverTheLimitEvictsTheLeastRecentlyUsed") = []
{
    auto location = createSampleStore("ssh-evicts", eacp::FilePath {"location"});
    auto oldest = storeUsedEntry(location, 0, 5);
    auto older = storeUsedEntry(location, 1, 4);
    auto local = createSampleStore("ssh-evicts-local", eacp::FilePath {"cache/lib"});
    auto sent = storeUsedEntry(local, 2, 3);
    auto limit = local.store.measureEntry(sent) + manifestAllowance;

    auto evicted = createFakeSshLocation(location.directory / "location")
                       .sendLibraries({sent}, local.store, {limit});

    check(evicted == 2);
    check(!location.store.findEntry(oldest) && !location.store.findEntry(older));
    check(location.store.findEntry(sent).has_value());
    check(location.store.findLastUse(sent) > getHoursAgo(1));
    check(!hasLeftOvers(location.directory));
};

auto tHostProtects =
    test("LocationOverSsh/aSendNeverEvictsWhatWasUsedInTheLastHour") = []
{
    auto location = createSampleStore("ssh-protects", eacp::FilePath {"location"});
    auto recent = storeUsedEntry(location, 0, 0);
    auto local =
        createSampleStore("ssh-protects-local", eacp::FilePath {"cache/lib"});
    auto sent = storeUsedEntry(local, 1, 3);

    auto evicted = createFakeSshLocation(location.directory / "location")
                       .sendLibraries({sent}, local.store, {1});

    check(evicted == 0);
    check(location.store.findEntry(recent) && location.store.findEntry(sent));
};

auto tFetchMarksUsed = test("LocationOverSsh/aFetchMarksWhatItAskedAboutUsed") = []
{
    auto location =
        createSampleStore("ssh-fetch-marks", eacp::FilePath {"location"});
    auto missing = storeUsedEntry(location, 0, 5);
    auto copied = storeUsedEntry(location, 1, 5);
    auto local =
        createSampleStore("ssh-fetch-marks-local", eacp::FilePath {"cache/lib"});

    createFakeSshLocation(location.directory / "location")
        .fetchLibraries({{missing}, false, {copied}}, local.store);

    check(location.store.findLastUse(missing) > getHoursAgo(1));
    check(location.store.findLastUse(copied) > getHoursAgo(1));
};

auto tTwoHostEvictors = test("LocationOverSsh/twoSendsEvictingAtOnceEndWell") = []
{
    auto location =
        createSampleStore("ssh-evict-concurrently", eacp::FilePath {"location"});

    for (auto index = 0; index < oldEntryCount; ++index)
        storeUsedEntry(location, index, oldEntryCount - index + 1);

    auto local = createSampleStore("ssh-evict-concurrently-local",
                                   eacp::FilePath {"cache/lib"});
    auto sent = storeUsedEntry(local, oldEntryCount, 0);
    auto limit =
        (local.store.measureEntry(sent) + manifestAllowance) * entriesThatFit;
    auto arguments = eacp::Vector<std::string> {
        std::string {librarySenderArgument},
        (local.directory / "cache").str(),
        "lockstep-fake-host:" + (location.directory / "location").str(),
        sent.key,
        std::to_string(limit)};
    auto first = eacp::Processes::Process {testExecutablePath, arguments};
    auto second = eacp::Processes::Process {testExecutablePath, arguments};

    check(first.launched() && second.launched());
    check(first.wait() == 0);
    check(second.wait() == 0);
    check(location.store.findEntry(sent).has_value());
    check(location.store.listEveryBuild().size() <= entriesThatFit);
    check(isEveryBuildWhole(location.store));
    check(!hasLeftOvers(location.directory));
};
#endif

int runStoreEvictor(std::string_view cacheDirectory, std::string_view limitBytes)
{
    auto store = lockstep::LibraryStore {eacp::FilePath {cacheDirectory}};
    lockstep::evictLeastRecentlyUsed(store, {lockstep::parseByteSize(limitBytes)});
    return 0;
}
