#include "FakeTools.h"
#include "TestDirectory.h"

#include <LibraryCacheLocation.h>
#include <LibraryStore.h>
#include <Sha256.h>

#include <NanoTest/NanoTest.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

using namespace nano;

namespace LocationOverSshTests
{
constexpr auto testExecutablePath = LOCKSTEP_TESTS_EXECUTABLE;
constexpr auto fakeHost = "lockstep-fake-host";

struct SshLocation
{
    eacp::FilePath directory;
    lockstep::LibraryStore store;
    lockstep::LibraryCacheAtLocation cache;
};

SshLocation createSshLocation(const eacp::FilePath& directory)
{
    auto location = *lockstep::parseLibraryCacheLocation(std::string {fakeHost} + ":"
                                                         + directory.str());
    return {directory,
            lockstep::LibraryStore {lockstep::LibraryStoreDirectory {directory}},
            lockstep::LibraryCacheAtLocation {
                location, eacp::FilePath {"cmake"}, testExecutablePath}};
}

lockstep::BuiltWith createBuiltWith(const std::string& compilerVersion)
{
    auto builtWith = lockstep::BuiltWith {};
    builtWith.compilers = {{"CXX", compilerVersion, "hash", ""}};
    return builtWith;
}

struct SampleBuilds
{
    std::string key = lockstep::computeSha256Hex("inputs");
    eacp::FilePath library;
};

SampleBuilds createSampleBuilds(const eacp::FilePath& directory)
{
    auto builds = SampleBuilds {};
    builds.library = directory / "build" / "libgreeter.a";
    writeTestFile(builds.library, "archive");
    return builds;
}

lockstep::EntryAddress storeBuild(const lockstep::LibraryStore& store,
                                  const SampleBuilds& builds,
                                  const std::string& compilerVersion)
{
    auto builtWith = createBuiltWith(compilerVersion);
    store.storeEntry(builds.key,
                     {lockstep::CachedTargetKind::staticLibrary,
                      {builds.library},
                      "inputs",
                      builtWith});
    return {builds.key, lockstep::computeBuildId(builtWith)};
}

lockstep::EntryAddress addressOf(const SampleBuilds& builds,
                                 const std::string& compilerVersion)
{
    return {builds.key, lockstep::computeBuildId(createBuiltWith(compilerVersion))};
}
} // namespace LocationOverSshTests

using namespace LocationOverSshTests;

#if !defined(_WIN32)
auto tFetchesOwnBuild = test("LocationOverSsh/fetchesTheBuildAskedFor") = []
{
    auto directory = createEmptyTestDirectory("ssh-fetch-own");
    auto builds = createSampleBuilds(directory);
    auto location = createSshLocation(directory / "location");
    auto own = storeBuild(location.store, builds, "21.0.0.1");
    storeBuild(location.store, builds, "21.0.0.2");
    auto local = lockstep::LibraryStore {directory / "cache"};

    auto fetched = location.cache.fetchLibraries({{own}, true, {}}, local);

    check(fetched.fetched == std::set {own});
    check(local.listBuilds(builds.key).size() == 1);
};

auto tFetchesOtherBuilds = test(
    "LocationOverSsh/fetchesEveryBuildOfAKeyWhenItsOwnIsMissingAndARangeIsSet") = []
{
    auto directory = createEmptyTestDirectory("ssh-fetch-others");
    auto builds = createSampleBuilds(directory);
    auto location = createSshLocation(directory / "location");
    auto first = storeBuild(location.store, builds, "21.0.0.1");
    auto second = storeBuild(location.store, builds, "21.0.0.2");
    auto missing = addressOf(builds, "21.0.0.3");
    auto local = lockstep::LibraryStore {directory / "cache"};

    auto withoutRange = location.cache.fetchLibraries({{missing}, false, {}}, local);
    auto withRange = location.cache.fetchLibraries({{missing}, true, {}}, local);

    check(withoutRange.fetched.empty());
    check(withRange.fetched == std::set {first, second});
    check(local.listBuilds(builds.key).size() == 2);
};

auto tNamesWhatItLacks = test("LocationOverSsh/namesTheCopiedBuildsItLacks") = []
{
    auto directory = createEmptyTestDirectory("ssh-lacks");
    auto builds = createSampleBuilds(directory);
    auto location = createSshLocation(directory / "location");
    auto held = storeBuild(location.store, builds, "21.0.0.1");
    auto lacked = addressOf(builds, "21.0.0.2");
    auto local = lockstep::LibraryStore {directory / "cache"};

    auto fetched = location.cache.fetchLibraries({{}, false, {held, lacked}}, local);

    check(fetched.locationLacks == std::vector {lacked});
};

auto tSends = test("LocationOverSsh/sendsBuildsBesideTheOnesThere") = []
{
    auto directory = createEmptyTestDirectory("ssh-send");
    auto builds = createSampleBuilds(directory);
    auto location = createSshLocation(directory / "location");
    auto there = storeBuild(location.store, builds, "21.0.0.1");
    auto local = lockstep::LibraryStore {directory / "cache"};
    auto sent = storeBuild(local, builds, "21.0.0.2");

    location.cache.sendLibraries({sent}, local);
    location.cache.sendLibraries({sent}, local);

    check(location.store.findEntry(there).has_value());
    check(location.store.findEntry(sent).has_value());
    check(location.store.listBuilds(builds.key).size() == 2);
};
#endif

int runFakeSsh(std::span<char*> arguments)
{
#if defined(_WIN32)
    static_cast<void>(arguments);
    return 1;
#else
    execl("/bin/sh", "sh", "-c", arguments.back(), static_cast<char*>(nullptr));
    return 1;
#endif
}
