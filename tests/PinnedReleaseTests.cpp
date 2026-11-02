#include "TestDirectory.h"

#include <PinnedRelease.h>

#include <NanoTest/NanoTest.h>

using namespace nano;

auto tReadsShimPins = test("PinnedRelease/readsVersionAndHostHash") = []
{
    auto directory = createEmptyTestDirectory("pinned-release");
    auto shim = directory / "lockstep.cmake";
    auto platform = lockstep::getHostPlatformName();
    writeTestFile(shim,
                  "set(LOCKSTEP_VERSION 1.2.3)\n"
                  "set(LOCKSTEP_RELEASE_URL https://example.com/releases)\n"
                  "set(LOCKSTEP_SHA256_"
                      + platform + " \"abcdef\")\n");

    auto release = lockstep::readPinnedRelease(shim);
    check(release.has_value());
    check(release->version == "1.2.3");
    check(release->sha256 == "abcdef");
    check(release->releaseUrl == "https://example.com/releases");
};

auto tFindsLockAbove = test("PinnedRelease/findsLockInParentFolder") = []
{
    auto directory = createEmptyTestDirectory("pinned-lock");
    writeTestFile(directory / "lockstep.lock", "{}");
    eacp::Files::createDirectories(directory / "a" / "b");

    auto found = lockstep::findEnclosingLockFile(directory / "a" / "b");
    check(found.has_value());
    check(found->fileName() == "lockstep.lock");
};
