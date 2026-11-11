#include "TestDirectory.h"

#include <BuiltWith.h>
#include <BuiltWithFields.h>
#include <CompileFlags.h>
#include <LibraryCachePlan.h>
#include <LibraryStore.h>
#include <VersionRange.h>

#include <NanoTest/NanoTest.h>

#include <stdexcept>
#include <string>

using namespace nano;

namespace ToolchainRangeTests
{
constexpr auto tamby = "21.0.0.21000101";
constexpr auto bigCow = "21.0.0.21000401";
const auto keyOfEveryBuild = std::string(lockstep::libraryKeyLength, 'b');

bool admits(std::string_view range, std::string_view version)
{
    return lockstep::parseVersionRange(range).admits(version);
}

bool isRefusedAsRange(std::string_view text)
{
    try
    {
        lockstep::parseVersionRange(text);
        return false;
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
}

lockstep::BuiltWith createAppleBuild(const std::string& compilerVersion,
                                     const std::string& sdkVersion)
{
    auto builtWith = lockstep::BuiltWith {};
    builtWith.compilers = {{"CXX", compilerVersion, "hash-" + compilerVersion, ""}};
    builtWith.sdkVersion = sdkVersion;
    builtWith.sdkBuildVersion = "build-" + sdkVersion;
    builtWith.cmakeVersion = "4.4.0";
    builtWith.hostSystemVersion = "25.5.0";
    return builtWith;
}

lockstep::ToolchainRanges createCompilerRange(std::string_view range)
{
    return {lockstep::parseVersionRange(range), std::nullopt};
}

bool isTaken(const lockstep::BuiltWith& stored,
             const lockstep::BuiltWith& thisMachine,
             const lockstep::ToolchainRanges& ranges)
{
    return !lockstep::findReasonToRefuseBuild(stored, thisMachine, ranges);
}

std::string findReason(const lockstep::BuiltWith& stored,
                       const lockstep::BuiltWith& thisMachine,
                       const lockstep::ToolchainRanges& ranges)
{
    return lockstep::findReasonToRefuseBuild(stored, thisMachine, ranges)
        .value_or("");
}

lockstep::IntermediateCode findIntermediateCodeOf(const std::string& compilerId,
                                                  const std::string& extraRecords)
{
    auto records = "directory\t/src\tCMAKE_CXX_COMPILER_ID\t" + compilerId
                   + "\n"
                     "directory\t/src\tCMAKE_BUILD_TYPE\tRelease\n"
                     "target\tgreeter\tSOURCE_DIR\t/src\n"
                     "target\tgreeter\tCOMPILED_LANGUAGES\tCXX\n"
                   + extraRecords;
    return lockstep::findIntermediateCode(
        lockstep::parseConfigureDescription(records), "greeter");
}

void storeBuild(const lockstep::LibraryStore& store,
                const eacp::FilePath& library,
                const lockstep::BuiltWith& builtWith)
{
    store.storeEntry(
        keyOfEveryBuild,
        {lockstep::CachedTargetKind::staticLibrary, {library}, "inputs", builtWith});
}

lockstep::LibraryKey createKeyBuiltWith(const lockstep::BuiltWith& builtWith)
{
    auto key = lockstep::createLibraryKey("inputs", builtWith);
    key.key = keyOfEveryBuild;
    return key;
}
} // namespace ToolchainRangeTests

using namespace ToolchainRangeTests;

auto tComparesUnequalLengths =
    test("VersionRange/comparesVersionsOfUnequalLength") = []
{
    auto version = [](std::string_view text)
    { return *lockstep::parseDottedVersion(text); };

    check(lockstep::compareVersions(version("21.0"), version("21.0.0.0"))
          == std::strong_ordering::equal);
    check(lockstep::compareVersions(version(bigCow), version(tamby))
          == std::strong_ordering::greater);
    check(lockstep::compareVersions(version("21.0.0"), version(tamby))
          == std::strong_ordering::less);
    check(lockstep::compareVersions(version("9.9"), version("10"))
          == std::strong_ordering::less);
};

auto tBoundComparesItsOwnLength =
    test("VersionRange/aBoundComparesAsManyNumbersAsItHas") = []
{
    check(admits("=21.0.0", tamby));
    check(admits("=21.0.0", bigCow));
    check(admits("=21.0.0", "21"));
    check(!admits("=21.0.0", "21.0.1"));
    check(admits("=21.0.0.21000101", tamby));
    check(!admits("=21.0.0.21000101", bigCow));

    check(admits("<=21.0.4", "21.0.4.9"));
    check(admits("<=21.0.4", "21.0.3"));
    check(!admits("<=21.0.4", "21.0.5"));
    check(!admits("<=21.0.4", "21.1"));

    check(admits(">=21.0.0", tamby));
    check(admits(">=26.5", "27.2"));
    check(admits(">=26.5", "26.5"));
    check(!admits(">=26.5", "26.4.1"));
};

auto tEveryPartHolds = test("VersionRange/everyPartOfARangeMustHold") = []
{
    check(admits(">=21.0.0 <=21.0.4", tamby));
    check(admits(">=21.0.0  <=21.0.4", "21.0.4"));
    check(!admits(">=21.0.0 <=21.0.4", "20.1"));
    check(!admits(">=21.0.0 <=21.0.4", "21.0.5"));
    check(!admits(">=21", "not a version"));
    check(!admits(">=21", ""));
};

auto tRefusesNonsense = test("VersionRange/refusesWhatIsNotARange") = []
{
    for (auto text: {"",
                     "   ",
                     "21.0",
                     "~21",
                     ">21",
                     "<21",
                     ">=",
                     ">=21.a",
                     ">=21..0",
                     ">=.21",
                     "=>21",
                     ">=21,<=22"})
        check(isRefusedAsRange(text));
};

auto tNoRangeTakesOwnBuildOnly =
    test("BuiltWith/withoutARangeOnlyThisMachinesOwnRecordIsTaken") = []
{
    auto thisMachine = createAppleBuild(bigCow, "27.2");
    auto otherCMake = thisMachine;
    otherCMake.cmakeVersion = "4.4.5";
    auto patchedCompiler = thisMachine;
    patchedCompiler.compilers.front().executableSha256 = "patched";

    check(isTaken(thisMachine, thisMachine, {}));
    check(!isTaken(createAppleBuild(tamby, "26.5"), thisMachine, {}));
    check(!isTaken(otherCMake, thisMachine, {}));
    check(!isTaken(patchedCompiler, thisMachine, {}));
    check(
        findReason(patchedCompiler, thisMachine, {}).find("CXX compiler executable")
        != std::string::npos);
};

auto tCompilerRange =
    test("BuiltWith/compilerRangeAdmitsBuildsOfOtherCompilers") = []
{
    auto bigCowSameSdk = createAppleBuild(bigCow, "26.5");
    auto tambyBuild = createAppleBuild(tamby, "26.5");
    tambyBuild.cmakeVersion = "4.4.5";
    tambyBuild.hostSystemVersion = "27.0.0";

    check(isTaken(tambyBuild, bigCowSameSdk, createCompilerRange(">=21.0.0")));
    check(isTaken(tambyBuild, bigCowSameSdk, createCompilerRange("=21.0.0")));
    check(!isTaken(
        tambyBuild, bigCowSameSdk, createCompilerRange("=21.0.0.21000401")));
    check(
        findReason(tambyBuild, bigCowSameSdk, createCompilerRange(">=21.0.1"))
        == "its CXX compiler 21.0.0.21000101 is outside the compiler range >=21.0.1");
};

auto tCompilerRangeSkipsHash =
    test("BuiltWith/compilerRangeDoesNotCompareTheCompilerExecutable") = []
{
    auto thisMachine = createAppleBuild(tamby, "26.5");
    auto patched = thisMachine;
    patched.compilers.front().executableSha256 = "patched";

    check(!isTaken(patched, thisMachine, {}));
    check(isTaken(patched, thisMachine, createCompilerRange("=21.0.0")));
};

auto tSdkStaysExactUnderCompilerRange =
    test("BuiltWith/sdkMustMatchUnlessTheSdkRangeAdmitsIt") = []
{
    auto bigCowBuild = createAppleBuild(bigCow, "27.2");
    auto tambyBuild = createAppleBuild(tamby, "26.5");
    auto bothRanges = lockstep::ToolchainRanges {
        lockstep::parseVersionRange(">=21"), lockstep::parseVersionRange(">=26.5")};
    auto sdkOnly = lockstep::ToolchainRanges {std::nullopt,
                                              lockstep::parseVersionRange(">=26.5")};
    auto sameCompilerOtherSdk = createAppleBuild(bigCow, "26.5");

    check(findReason(tambyBuild, bigCowBuild, createCompilerRange(">=21"))
          == "it was built with SDK 26.5 (build-26.5), and "
             "LOCKSTEP_LIBRARY_CACHE_SDK_RANGE is not set");
    check(isTaken(tambyBuild, bigCowBuild, bothRanges));
    check(!isTaken(tambyBuild, bigCowBuild, sdkOnly));
    check(isTaken(sameCompilerOtherSdk, bigCowBuild, sdkOnly));
    check(!isTaken(sameCompilerOtherSdk,
                   bigCowBuild,
                   {std::nullopt, lockstep::parseVersionRange(">=27")}));
};

auto tNewerBitcodeRefused = test(
    "BuiltWith/linkTimeOptimisedBuildFromANewerClangIsRefusedWhateverTheRange") = []
{
    auto older = createAppleBuild(tamby, "26.5");
    auto newer = createAppleBuild(bigCow, "26.5");
    newer.intermediateCode = lockstep::IntermediateCode::readableByNewerCompilers;
    auto olderOptimised = older;
    olderOptimised.intermediateCode =
        lockstep::IntermediateCode::readableByNewerCompilers;
    auto range = createCompilerRange(">=21");

    check(
        findReason(newer, olderOptimised, range)
        == "it holds link-time optimisation code from CXX compiler 21.0.0.21000401, "
           "newer than this machine's 21.0.0.21000101, whose linker cannot read it");
    check(isTaken(olderOptimised, newer, range));
};

auto tSameVersionOnly =
    test("BuiltWith/msvcAndGccIntermediateCodeNeedsTheExactCompilerVersion") = []
{
    auto thisMachine = createAppleBuild("19.44.35211", "");
    thisMachine.intermediateCode =
        lockstep::IntermediateCode::readableBySameCompilerVersionOnly;
    auto older = thisMachine;
    older.compilers.front().version = "19.44.35207";
    older.compilers.front().executableSha256 = "older";
    auto patchedSameVersion = thisMachine;
    patchedSameVersion.compilers.front().executableSha256 = "patched";
    auto range = createCompilerRange(">=19");

    check(findReason(older, thisMachine, range)
              .find("only that compiler version reads")
          != std::string::npos);
    check(isTaken(patchedSameVersion, thisMachine, range));
};

auto tExecutableLinkedBuilds = test(
    "BuiltWith/executableIsTakenOnlyWithTheLibraryBuildsThisDirectoryLinks") = []
{
    auto thisMachine = createAppleBuild(bigCow, "26.5");
    thisMachine.linkedBuilds = {"key/own-build"};
    auto stored = createAppleBuild(tamby, "26.5");
    stored.linkedBuilds = {"key/other-build"};
    auto sameLibraries = stored;
    sameLibraries.linkedBuilds = thisMachine.linkedBuilds;
    auto range = createCompilerRange(">=21");

    check(!isTaken(stored, thisMachine, range));
    check(isTaken(sameLibraries, thisMachine, range));
};

auto tFindsIntermediateCode = test("CompileFlags/findsLinkTimeOptimisation") = []
{
    using Code = lockstep::IntermediateCode;
    check(findIntermediateCodeOf("Clang", "") == Code::none);
    check(findIntermediateCodeOf("Clang",
                                 "target\tgreeter\tCOMPILE_OPTIONS\t-flto=thin\n")
          == Code::readableByNewerCompilers);
    check(findIntermediateCodeOf("AppleClang",
                                 "directory\t/src\tCMAKE_CXX_FLAGS\t-O2 -flto\n")
          == Code::readableByNewerCompilers);
    check(findIntermediateCodeOf(
              "AppleClang", "target\tgreeter\tINTERPROCEDURAL_OPTIMIZATION\tON\n")
          == Code::readableByNewerCompilers);
    check(
        findIntermediateCodeOf(
            "Clang", "target\tgreeter\tINTERPROCEDURAL_OPTIMIZATION_RELEASE\tTRUE\n")
        == Code::readableByNewerCompilers);
    check(findIntermediateCodeOf(
              "Clang", "target\tgreeter\tINTERPROCEDURAL_OPTIMIZATION_DEBUG\tTRUE\n")
          == Code::none);
    check(findIntermediateCodeOf("GNU",
                                 "target\tgreeter\tCOMPILE_OPTIONS\t-flto=auto\n")
          == Code::readableBySameCompilerVersionOnly);
    check(findIntermediateCodeOf("MSVC", "target\tgreeter\tCOMPILE_OPTIONS\t/GL\n")
          == Code::readableBySameCompilerVersionOnly);
    check(findIntermediateCodeOf("MSVC", "target\tgreeter\tCOMPILE_OPTIONS\t/GL-\n")
          == Code::none);
    check(
        findIntermediateCodeOf("GNU", "target\tgreeter\tCOMPILE_OPTIONS\t-fno-lto\n")
        == Code::none);
};

auto tPrefersOwnBuild = test("LibraryCachePlan/prefersThisMachinesOwnBuild") = []
{
    auto directory = createEmptyTestDirectory("choose-own-build");
    auto library = directory / "libgreeter.a";
    writeTestFile(library, "archive");
    auto store = lockstep::LibraryStore {directory / "cache"};
    auto own = createAppleBuild(tamby, "26.5");
    storeBuild(store, library, createAppleBuild(bigCow, "26.5"));
    storeBuild(store, library, own);

    auto choice =
        lockstep::chooseStoredBuild(store,
                                    createKeyBuiltWith(own),
                                    lockstep::CachedTargetKind::staticLibrary,
                                    createCompilerRange(">=21"));

    check(choice.entry
          && choice.entry->address.buildId == lockstep::computeBuildId(own));
};

auto tChoosesNewestAdmitted =
    test("LibraryCachePlan/takesTheNewestAdmittedBuildAndSaysWhyOthersAreNot") = []
{
    auto directory = createEmptyTestDirectory("choose-newest-build");
    auto library = directory / "libgreeter.a";
    writeTestFile(library, "archive");
    auto store = lockstep::LibraryStore {directory / "cache"};
    auto newest = createAppleBuild("21.0.0.21000301", "26.5");
    storeBuild(store, library, createAppleBuild(tamby, "26.5"));
    storeBuild(store, library, newest);
    storeBuild(store, library, createAppleBuild("20.1.0", "26.5"));
    auto key = createKeyBuiltWith(createAppleBuild(bigCow, "26.5"));

    auto choice =
        lockstep::chooseStoredBuild(store,
                                    key,
                                    lockstep::CachedTargetKind::staticLibrary,
                                    createCompilerRange(">=21"));
    auto unranged = lockstep::chooseStoredBuild(
        store, key, lockstep::CachedTargetKind::staticLibrary, {});

    check(choice.entry
          && choice.entry->address.buildId == lockstep::computeBuildId(newest));
    check(choice.refusals.size() == 1
          && choice.refusals.front().find("20.1.0 is outside") != std::string::npos);
    check(!unranged.entry && unranged.refusals.size() == 3);
};
