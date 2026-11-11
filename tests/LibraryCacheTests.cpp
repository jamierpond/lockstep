#include "FakeArchiver.h"
#include "LibrarySender.h"
#include "StatisticsWriter.h"
#include "TestDirectory.h"

#include <BuiltWithFields.h>
#include <LibraryCache.h>
#include <LibraryCacheLocation.h>
#include <LibraryCachePlan.h>
#include <LibraryStore.h>
#include <ByteSize.h>
#include <Sha256.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/StdPath.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace nano;

namespace
{
constexpr auto packageHash = "1111";
constexpr auto cacheDirectory = std::string_view {"/cache"};
constexpr auto packageDirectory = "/cache/src/1111";
constexpr auto projectDirectory = "/project";
constexpr auto testExecutablePath = LOCKSTEP_TESTS_EXECUTABLE;
const auto sampleKey = std::string(lockstep::libraryKeyLength, 'a');

// The id of a build stored with an empty record, as the tests' entries are.
const auto plainBuildId = lockstep::computeBuildId({});

lockstep::EntryAddress addressOf(const std::string& key)
{
    return {key, plainBuildId};
}
constexpr auto fakeArchiverPatience = std::chrono::seconds {60};
constexpr auto sizeNoTestReaches = std::string_view {"1 TiB"};
constexpr auto filePollInterval = std::chrono::milliseconds {20};

bool waitForFile(const eacp::FilePath& path)
{
    auto deadline = std::chrono::steady_clock::now() + fakeArchiverPatience;

    while (!eacp::Files::isFile(path))
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;

        std::this_thread::sleep_for(filePollInterval);
    }

    return true;
}

eacp::FilePath getCurrentFormatStore(const eacp::FilePath& cache)
{
    return cache / "lib"
           / ("format-" + std::to_string(lockstep::libraryCacheFormatVersion));
}

struct SampleBuild
{
    std::string cacheDirectory {::cacheDirectory};
    std::string buildDirectory = "/first-build";
    std::string definitions = "GREETING=1";
    std::string loudValue = "OFF";
    std::string callArguments =
        "NAME;greeter;GIT_TAG;develop;OPTIONS;GREETER_LOUD OFF";
    std::string callArgumentCount = "6";
    std::string firstCallArgument = "NAME";
    std::string linkLibraries = "fmt";
    std::string system = "OFF";
    std::string cxxStandardRequired = "ON";
    std::string compiledLanguages = "CXX";
    std::string generator = "Unix Makefiles";
    std::string scanForModules = "OFF";
    std::string cCompilerVersion = "13.2.0";
    std::string cFlags = "-O2";
    std::string topLevelCCompilerVersion;
    std::string apple;
    std::string appleSdkVersion = "26.5";
    std::string appleSdkBuildVersion = "25F70";
    std::string cmakeVersion = "4.4.0";
    std::string systemVersion = "25.5.0";
    std::string crossCompiling = "FALSE";
};

std::string createDirectoryRecords(const SampleBuild& build)
{
    return "directory\t/project\tCMAKE_C_COMPILER_VERSION\t"
           + build.topLevelCCompilerVersion
           + "\n"
             "directory\t/cache/src/1111\tCMAKE_CXX_COMPILER_ID\tGNU\n"
             "directory\t/cache/src/1111\tCMAKE_C_COMPILER_VERSION\t"
           + build.cCompilerVersion
           + "\n"
             "directory\t/cache/src/1111\tCMAKE_C_FLAGS\t"
           + build.cFlags
           + "\n"
             "directory\t/cache/src/1111\tCMAKE_GENERATOR\t"
           + build.generator
           + "\n"
             "directory\t/cache/src/1111\tAPPLE\t"
           + build.apple
           + "\n"
             "directory\t/cache/src/1111\tAPPLE_SDK_VERSION\t"
           + build.appleSdkVersion
           + "\n"
             "directory\t/cache/src/1111\tAPPLE_SDK_BUILD_VERSION\t"
           + build.appleSdkBuildVersion
           + "\n"
             "directory\t/cache/src/1111\tCMAKE_VERSION\t"
           + build.cmakeVersion
           + "\n"
             "directory\t/cache/src/1111\tCMAKE_SYSTEM_VERSION\t"
           + build.systemVersion
           + "\n"
             "directory\t/cache/src/1111\tCMAKE_CROSSCOMPILING\t"
           + build.crossCompiling + "\n";
}

std::string createTargetRecords(const SampleBuild& build)
{
    return "target\tgreeter\tSYSTEM\t" + build.system
           + "\n"
             "target\tgreeter\tCXX_STANDARD_REQUIRED\t"
           + build.cxxStandardRequired
           + "\n"
             "target\tgreeter\tCXX_SCAN_FOR_MODULES\t"
           + build.scanForModules
           + "\n"
             "target\tgreeter\tCOMPILED_LANGUAGES\t"
           + build.compiledLanguages
           + "\n"
             "target\tgreeter\tTYPE\tSTATIC_LIBRARY\n"
             "target\tgreeter\tSOURCE_DIR\t/cache/src/1111\n"
             "target\tgreeter\tBINARY_DIR\t"
           + build.buildDirectory
           + "/_deps/greeter-build\n"
             "target\tgreeter\tCOMPILE_DEFINITIONS\t"
           + build.definitions
           + "\n"
             "target\tgreeter\tLINK_LIBRARIES\t"
           + build.linkLibraries
           + "\n"
             "target\tgreeter\tLINKED_TARGETS\tfmt\n"
             "target\tgreeter\tSTUB_LANGUAGE\tCXX\n"
             "target\tfmt\tTYPE\tSTATIC_LIBRARY\n"
             "target\tfmt\tSOURCE_DIR\t/cache/src/1111/fmt\n"
             "target\tfmt\tINTERFACE_LINKED_TARGETS\tThreads::Threads\n"
             "target\tThreads::Threads\tIMPORTED\tTRUE\n";
}

std::string createVariableRecords(const SampleBuild& build)
{
    return "variable\t/cache/src/1111\tGREETER_LOUD\t" + build.loudValue
           + "\n"
             "variable\t/cache/src/1111\tARGN\t"
           + build.callArguments
           + "\n"
             "variable\t/cache/src/1111\tARGV\t"
           + build.callArguments
           + "\n"
             "variable\t/cache/src/1111\tARGC\t"
           + build.callArgumentCount
           + "\n"
             "variable\t/cache/src/1111\tARGV0\t"
           + build.firstCallArgument
           + "\n"
             "variable\t/cache/src/1111\tUNRELATED_SETTING\t"
           + build.buildDirectory + "\n";
}

std::string createRecords(const SampleBuild& build)
{
    return "global\t-\tENABLED_LANGUAGES\tC;CXX\n" + createDirectoryRecords(build)
           + createTargetRecords(build) + createVariableRecords(build);
}

// The records with every path under the sample cache moved to the build's own
// cache directory.
std::string createRecordsInCacheDirectory(const SampleBuild& build)
{
    auto records = createRecords(build);
    auto from = std::string {cacheDirectory} + "/";
    auto to = build.cacheDirectory + "/";

    for (auto position = records.find(from); position != std::string::npos;
         position = records.find(from, position + to.size()))
        records.replace(position, from.size(), to);

    return records;
}

lockstep::LibraryKey computeSampleKey(const SampleBuild& build)
{
    auto description =
        lockstep::parseConfigureDescription(createRecordsInCacheDirectory(build));
    auto packageSources = build.cacheDirectory + "/src/" + packageHash;
    auto packages = std::vector<lockstep::ConfiguredPackage> {
        {"greeter", packageHash, packageSources}};
    auto names = lockstep::PackageVariableNames {
        {packageHash, {"GREETER_LOUD", "ARGN", "ARGV", "ARGC", "ARGV0"}}};
    auto inputs = lockstep::LibraryCacheInputs {
        description, packages, names, {build.buildDirectory, build.cacheDirectory}};
    return lockstep::computeLibraryKey(inputs, "greeter");
}

// <key>/<build id>: where this build's entry is looked up.
std::string computeAddress(const SampleBuild& build)
{
    auto key = computeSampleKey(build);
    return lockstep::EntryAddress {key.key, key.buildId}.getPath();
}

bool storeLibrary(const lockstep::LibraryStore& store,
                  const std::string& key,
                  const eacp::FilePath& library,
                  const std::string& inputs)
{
    return store.storeEntry(
        key, {lockstep::CachedTargetKind::staticLibrary, {library}, inputs});
}

std::optional<eacp::FilePath> findLibrary(const lockstep::LibraryStore& store,
                                          const std::string& key)
{
    auto entry = store.findEntry(addressOf(key));

    if (!entry)
        return std::nullopt;

    return entry->getFilePaths().front();
}

lockstep::LibraryCacheStatistics
    countStaticLibraries(const lockstep::LibraryCacheCounts& counts)
{
    auto statistics = lockstep::LibraryCacheStatistics {};
    statistics.staticLibraries = counts;
    return statistics;
}

int countStoredLibraries(const lockstep::LibraryStoreSummary& summary)
{
    auto found = summary.entryCounts.find(lockstep::CachedTargetKind::staticLibrary);

    if (found == summary.entryCounts.end())
        return 0;

    return found->second;
}

std::optional<std::string> findReason(const std::string& records)
{
    auto description = lockstep::parseConfigureDescription(records);
    auto packages = std::vector<lockstep::ConfiguredPackage> {
        {"greeter", packageHash, packageDirectory}, {"local", "", projectDirectory}};
    auto names = lockstep::PackageVariableNames {};
    auto inputs = lockstep::LibraryCacheInputs {
        description, packages, names, {"/build", "/cache"}};
    return lockstep::findReasonNotToCache(inputs, "greeter");
}
} // namespace

auto tUnescapes = test("LibraryCache/parsesEscapedRecordValues") = []
{
    auto description = lockstep::parseConfigureDescription(
        "target\tgreeter\tCOMPILE_OPTIONS\ta\\\\b\\nc\\td\n");
    auto value =
        lockstep::findField(description.targets["greeter"], "COMPILE_OPTIONS");
    check(value == "a\\b\nc\td");
};

auto tWindowsLineEnds = test("LibraryCache/parsesRecordsWithWindowsLineEnds") = []
{
    auto description = lockstep::parseConfigureDescription(
        "target\tgreeter\tSOURCE_DIR\t/src/greeter\r\n"
        "target\tgreeter\tCOMPILE_OPTIONS\ta\\r\r\n");
    const auto& fields = description.targets["greeter"];
    check(lockstep::findField(fields, "SOURCE_DIR") == "/src/greeter");
    check(lockstep::findField(fields, "COMPILE_OPTIONS") == "a\r");
};

auto tSameKeyAcrossBuilds = test("LibraryCache/keyIgnoresBuildDirectory") = []
{
    auto second = SampleBuild {};
    second.buildDirectory = "/second-build";
    check(computeAddress(SampleBuild {}) == computeAddress(second));
};

auto tIgnoresDirectoryIds = test("LibraryCache/keyIgnoresDirectoryIds") = []
{
    auto first = SampleBuild {};
    first.linkLibraries = "::@(0x55d0c0ffee);fmt;::@";
    auto second = first;
    second.linkLibraries = "::@(0x7f00deadbeef);fmt;::@";
    check(computeAddress(first) == computeAddress(second));
};

auto tIgnoresTryCompileNames =
    test("LibraryCache/keyIgnoresTryCompileProjectNames") = []
{
    auto first = SampleBuild {};
    first.loudValue = "ninja -v cmTC_bdb97\nCMakeFiles/cmTC_bdb97.dir/main.c.o";
    auto second = first;
    second.loudValue = "ninja -v cmTC_84289\nCMakeFiles/cmTC_84289.dir/main.c.o";
    check(computeAddress(first) == computeAddress(second));
};

auto tDefinitionMisses = test("LibraryCache/keyChangesWithCompileDefinition") = []
{
    auto changed = SampleBuild {};
    changed.definitions = "GREETING=2";
    check(computeAddress(SampleBuild {}) != computeAddress(changed));
};

auto tVariableMisses =
    test("LibraryCache/keyChangesWithVariableThePackageNames") = []
{
    auto changed = SampleBuild {};
    changed.loudValue = "ON";
    check(computeAddress(SampleBuild {}) != computeAddress(changed));
};

auto tIgnoresCallText = test("LibraryCache/keyIgnoresTextOfAddPackageCall") = []
{
    auto otherCall = SampleBuild {};
    otherCall.callArguments =
        "GIT_TAG;0123456789abcdef0123456789abcdef01234567;NAME;greeter;"
        "OPTIONS;GREETER_LOUD OFF;SYSTEM;YES";
    otherCall.callArgumentCount = "8";
    otherCall.firstCallArgument = "GIT_TAG";
    check(computeAddress(SampleBuild {}) == computeAddress(otherCall));
};

auto tOptionInCallMisses =
    test("LibraryCache/keyChangesWithOptionValueOfAddPackageCall") = []
{
    auto loud = SampleBuild {};
    loud.callArguments = "NAME;greeter;GIT_TAG;develop;OPTIONS;GREETER_LOUD ON";
    loud.loudValue = "ON";
    check(computeAddress(SampleBuild {}) != computeAddress(loud));
};

auto tIgnoresGenerator =
    test("LibraryCache/keyIgnoresGeneratorWithoutModuleScanning") = []
{
    auto ninja = SampleBuild {};
    ninja.generator = "Ninja";
    check(computeAddress(SampleBuild {}) == computeAddress(ninja));
};

auto tScanningGeneratorMisses =
    test("LibraryCache/keyChangesWithGeneratorWhenScanningForModules") = []
{
    auto scanned = SampleBuild {};
    scanned.scanForModules = "";
    auto scannedNinja = scanned;
    scannedNinja.generator = "Ninja";
    check(computeAddress(scanned) != computeAddress(scannedNinja));
};

auto tIgnoresSystem = test("LibraryCache/keyIgnoresSystemProperty") = []
{
    auto system = SampleBuild {};
    system.system = "ON";
    check(computeAddress(SampleBuild {}) == computeAddress(system));
};

auto tIgnoresStandardRequired =
    test("LibraryCache/keyIgnoresCxxStandardRequired") = []
{
    auto notRequired = SampleBuild {};
    notRequired.cxxStandardRequired = "OFF";
    check(computeAddress(SampleBuild {}) == computeAddress(notRequired));
};

auto tIgnoresOtherLanguage =
    test("LibraryCache/keyIgnoresSettingsOfLanguageTargetDoesNotCompile") = []
{
    auto otherC = SampleBuild {};
    otherC.cCompilerVersion = "14.1.0";
    otherC.cFlags = "-O3 -DOTHER";
    check(computeAddress(SampleBuild {}) == computeAddress(otherC));
};

auto tCompiledLanguageMisses =
    test("LibraryCache/keyChangesWithSettingsOfLanguageTargetCompiles") = []
{
    auto withC = SampleBuild {};
    withC.compiledLanguages = "C;CXX";
    auto otherC = withC;
    otherC.cCompilerVersion = "14.1.0";
    auto otherFlags = withC;
    otherFlags.cFlags = "-O3";
    check(computeAddress(withC) != computeAddress(otherC));
    check(computeAddress(withC) != computeAddress(otherFlags));
};

auto tReadsTargetDirectory =
    test("LibraryCache/keyReadsLanguageSettingsOfTargetDirectory") = []
{
    auto withC = SampleBuild {};
    withC.compiledLanguages = "C;CXX";
    auto otherTopLevel = withC;
    otherTopLevel.topLevelCCompilerVersion = "14.1.0";
    auto otherPackage = withC;
    otherPackage.cCompilerVersion = "14.1.0";
    check(computeAddress(withC) == computeAddress(otherTopLevel));
    check(computeAddress(withC) != computeAddress(otherPackage));
};

auto tAppleSdk = test("LibraryCache/keyChangesWithAppleSdkOnAppleOnly") = []
{
    auto apple = SampleBuild {};
    apple.apple = "1";
    auto otherSdkVersion = apple;
    otherSdkVersion.appleSdkVersion = "26.6";
    auto otherSdkBuild = apple;
    otherSdkBuild.appleSdkBuildVersion = "25G5";
    auto notApple = SampleBuild {};
    notApple.appleSdkBuildVersion = "25G5";

    check(computeAddress(apple) != computeAddress(otherSdkVersion));
    check(computeAddress(apple) != computeAddress(otherSdkBuild));
    check(computeAddress(SampleBuild {}) == computeAddress(notApple));
};

auto tVersionsInRecord = test(
    "LibraryCache/compilerSdkCMakeAndSystemVersionsAreInTheRecordNotTheKey") = []
{
    auto apple = SampleBuild {};
    apple.apple = "1";
    apple.compiledLanguages = "C;CXX";
    auto otherVersions = apple;
    otherVersions.cCompilerVersion = "14.1.0";
    otherVersions.appleSdkVersion = "26.6";
    otherVersions.appleSdkBuildVersion = "25G5";
    otherVersions.cmakeVersion = "4.4.5";
    otherVersions.systemVersion = "27.0.0";
    auto first = computeSampleKey(apple);
    auto second = computeSampleKey(otherVersions);

    check(first.key == second.key);
    check(first.buildId != second.buildId);
    check(second.builtWith.sdkVersion == "26.6"
          && second.builtWith.sdkBuildVersion == "25G5");
    check(second.builtWith.cmakeVersion == "4.4.5");
    check(second.builtWith.hostSystemVersion == "27.0.0");
    check(second.builtWith.compilers.size() == 2
          && second.builtWith.compilers.front().language == "C"
          && second.builtWith.compilers.front().version == "14.1.0");
    check(first.inputs.find("COMPILER_VERSION") == std::string::npos);
};

auto tCrossCompiledSystemVersion =
    test("LibraryCache/crossCompilingKeepsTheSystemVersionInTheKey") = []
{
    auto cross = SampleBuild {};
    cross.crossCompiling = "TRUE";
    auto otherSystem = cross;
    otherSystem.systemVersion = "34";
    check(computeSampleKey(cross).key != computeSampleKey(otherSystem).key);
    check(computeSampleKey(cross).builtWith.hostSystemVersion.empty());
};

auto tCacheDirectory = test("LibraryCache/keyChangesWithCacheDirectory") = []
{
    auto otherCache = SampleBuild {};
    otherCache.cacheDirectory = "/other-cache";
    check(computeAddress(SampleBuild {}) != computeAddress(otherCache));
};

auto tMappedCacheDirectory =
    test("LibraryCache/keyIgnoresCacheDirectoryWhenFlagsMapItAway") = []
{
    // Each build's flags name its own cache directory where they say {cache}.
    auto keysMatchAcrossCaches = [](const std::string& flags)
    {
        auto withCache = [&](const std::string& cache)
        {
            auto build = SampleBuild {};
            build.compiledLanguages = "C";
            build.cacheDirectory = cache;
            build.cFlags = flags;
            auto placeholder = build.cFlags.find("{cache}");

            while (placeholder != std::string::npos)
            {
                build.cFlags.replace(
                    placeholder, std::string_view {"{cache}"}.size(), cache);
                placeholder = build.cFlags.find("{cache}");
            }

            return build;
        };

        return computeAddress(withCache("/cache"))
               == computeAddress(withCache("/other-cache"));
    };

    check(keysMatchAcrossCaches("-O2 -ffile-prefix-map={cache}=."));
    check(keysMatchAcrossCaches("-ffile-prefix-map=/=/root"));
    check(keysMatchAcrossCaches(
        "-fmacro-prefix-map={cache}/=. -fdebug-prefix-map={cache}=."));
    check(!keysMatchAcrossCaches("-fmacro-prefix-map={cache}=."));
    check(!keysMatchAcrossCaches("-fdebug-prefix-map={cache}=."));
    check(!keysMatchAcrossCaches("-ffile-prefix-map={cache}/src/1111=."));
    check(!keysMatchAcrossCaches("-ffile-prefix-map={cache}x=."));

    auto cxxBuild = SampleBuild {};
    cxxBuild.cFlags = "-ffile-prefix-map=/=/root";
    auto cxxBuildInOtherCache = cxxBuild;
    cxxBuildInOtherCache.cacheDirectory = "/other-cache";
    check(computeAddress(cxxBuild) != computeAddress(cxxBuildInOtherCache));
};

auto tClosure = test("LibraryCache/closureFollowsInterfaceLinks") = []
{
    auto description = lockstep::parseConfigureDescription(createRecords({}));
    auto closure = lockstep::collectLinkClosure(description, "greeter");
    check(closure == std::set<std::string> {"Threads::Threads", "fmt"});
};

auto tSkipsLocal = test("LibraryCache/skipsLinkToTargetOutsideCachedPackages") = []
{
    auto reason = findReason("target\tgreeter\tSOURCE_DIR\t/cache/src/1111\n"
                             "target\tgreeter\tSTUB_LANGUAGE\tCXX\n"
                             "target\tgreeter\tLINKED_TARGETS\thelper\n"
                             "target\thelper\tSOURCE_DIR\t/project/helper\n");
    check(reason.has_value() && reason->find("links helper") != std::string::npos);
};

auto tSkipsObjects = test("LibraryCache/skipsTargetUsedThroughTargetObjects") = []
{
    auto reason =
        findReason("target\tgreeter\tTYPE\tSTATIC_LIBRARY\n"
                   "target\tgreeter\tSOURCE_DIR\t/cache/src/1111\n"
                   "target\tgreeter\tSTUB_LANGUAGE\tCXX\n"
                   "target\tapp\tSOURCES\tmain.cpp;$<TARGET_OBJECTS:greeter>\n");
    check(reason.has_value() && reason->find("TARGET_OBJECTS") != std::string::npos);
};

auto tCachesObjectLibrary =
    test("LibraryCache/cachesObjectLibraryUsedThroughTargetObjects") = []
{
    check(!findReason("target\tgreeter\tTYPE\tOBJECT_LIBRARY\n"
                      "target\tgreeter\tSOURCE_DIR\t/cache/src/1111\n"
                      "target\tgreeter\tSTUB_LANGUAGE\tCXX\n"
                      "target\tapp\tSOURCES\tmain.cpp;$<TARGET_OBJECTS:greeter>\n"));
};

auto tSkipsSeparateProgramDatabase =
    test("LibraryCache/skipsMsvcLibraryWithSeparateProgramDatabase") = []
{
    auto msvcLibrary = [](const std::string& buildType, const std::string& extra)
    {
        return findReason("directory\t/cache/src/1111\tCMAKE_CXX_COMPILER_ID\tMSVC\n"
                          "directory\t/cache/src/1111\tCMAKE_BUILD_TYPE\t"
                          + buildType
                          + "\n"
                            "target\tgreeter\tSOURCE_DIR\t/cache/src/1111\n"
                            "target\tgreeter\tSTUB_LANGUAGE\tCXX\n"
                            "target\tgreeter\tCOMPILED_LANGUAGES\tCXX\n"
                          + extra);
    };
    auto isSkippedForProgramDatabase = [](const std::optional<std::string>& reason)
    { return reason.has_value() && reason->find(".pdb") != std::string::npos; };

    check(isSkippedForProgramDatabase(msvcLibrary("Debug", "")));
    check(isSkippedForProgramDatabase(msvcLibrary("RelWithDebInfo", "")));
    check(!msvcLibrary("Release", ""));
    check(isSkippedForProgramDatabase(
        msvcLibrary("Release", "target\tgreeter\tCOMPILE_OPTIONS\t/O2;/Zi\n")));
    check(isSkippedForProgramDatabase(msvcLibrary(
        "Release",
        "directory\t/cache/src/1111\tCMAKE_CXX_FLAGS_RELEASE\t/O2 -ZI\n")));
    check(isSkippedForProgramDatabase(msvcLibrary(
        "Release",
        "target\tgreeter\tMSVC_DEBUG_INFORMATION_FORMAT\tEditAndContinue\n")));
    check(isSkippedForProgramDatabase(
        msvcLibrary("Debug",
                    "target\tgreeter\tMSVC_DEBUG_INFORMATION_FORMAT\t"
                    "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>\n")));
    check(!msvcLibrary("Release",
                       "target\tgreeter\tMSVC_DEBUG_INFORMATION_FORMAT\t"
                       "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>\n"));
    check(!msvcLibrary(
        "Debug", "target\tgreeter\tMSVC_DEBUG_INFORMATION_FORMAT\tEmbedded\n"));
    check(!msvcLibrary("Debug",
                       "target\tgreeter\tMSVC_DEBUG_INFORMATION_FORMAT\tEmbedded\n"
                       "target\tgreeter\tCOMPILE_OPTIONS\t/Z7;/I/src/Zip\n"));
};

auto tIgnoresProgramDatabaseOffMsvc =
    test("LibraryCache/cachesLibraryWithProgramDatabaseFormatOffMsvc") = []
{
    check(!findReason(
        "directory\t/cache/src/1111\tCMAKE_CXX_COMPILER_ID\tGNU\n"
        "directory\t/cache/src/1111\tCMAKE_BUILD_TYPE\tDebug\n"
        "target\tgreeter\tSOURCE_DIR\t/cache/src/1111\n"
        "target\tgreeter\tSTUB_LANGUAGE\tCXX\n"
        "target\tgreeter\tCOMPILED_LANGUAGES\tCXX\n"
        "target\tgreeter\tMSVC_DEBUG_INFORMATION_FORMAT\tProgramDatabase\n"));
};

auto tSkipsSplitDebugInformation =
    test("LibraryCache/skipsLibraryWithSplitDebugInformation") = []
{
    auto libraryWithFlags = [](const std::string& flags)
    {
        return findReason("directory\t/cache/src/1111\tCMAKE_CXX_COMPILER_ID\tGNU\n"
                          "directory\t/cache/src/1111\tCMAKE_CXX_FLAGS\t"
                          + flags
                          + "\n"
                            "target\tgreeter\tSOURCE_DIR\t/cache/src/1111\n"
                            "target\tgreeter\tSTUB_LANGUAGE\tCXX\n"
                            "target\tgreeter\tCOMPILED_LANGUAGES\tCXX\n");
    };
    auto isSkippedForDwo = [](const std::optional<std::string>& reason)
    { return reason.has_value() && reason->find(".dwo") != std::string::npos; };

    check(isSkippedForDwo(libraryWithFlags("-g -gsplit-dwarf")));
    check(isSkippedForDwo(libraryWithFlags("-gsplit-dwarf=split")));
    check(!libraryWithFlags("-g -gsplit-dwarf=single"));
    check(!libraryWithFlags("-g"));
};

auto tCachesPlainLibrary = test("LibraryCache/cachesLibraryOfCachedPackage") = []
{
    check(!findReason("target\tgreeter\tSOURCE_DIR\t/cache/src/1111\n"
                      "target\tgreeter\tSTUB_LANGUAGE\tCXX\n"));
};

auto tIdentifiers = test("LibraryCache/collectsUpperCaseIdentifiers") = []
{
    auto names = lockstep::collectUpperCaseIdentifiers(
        "if (EACP_PCH AND NOT ${Mixed_Name})\n  set(lower ON)\nendif ()");
    check(names == std::set<std::string> {"AND", "EACP_PCH", "NOT", "ON"});
};

auto tCommandInput = test("LibraryCache/findsLibraryInCommandAndResponseFile") = []
{
    auto directory = createEmptyTestDirectory("library-cache-command");
    auto library = directory / "lib" / "libgreeter.a";
    writeTestFile(library, "archive");
    writeTestFile(directory / "link.rsp", "-o sample \"lib/libgreeter.a\" -lm\n");

    check(lockstep::isCommandInput({"c++", "lib/libgreeter.a"}, library, directory));
    check(lockstep::isCommandInput({"c++", "@link.rsp"}, library, directory));
    check(!lockstep::isCommandInput(
        {"c++", "other/libgreeter.a"}, library, directory));
};

auto tStoreRoundTrip = test("LibraryStore/storesAndFindsLibrary") = []
{
    auto directory = createEmptyTestDirectory("library-store");
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, "first");
    auto store = lockstep::LibraryStore {directory / "cache"};

    check(!findLibrary(store, "key"));
    check(storeLibrary(store, "key", library, "inputs"));
    writeTestFile(library, "second");
    check(!storeLibrary(store, "key", library, "inputs"));

    auto found = findLibrary(store, "key");
    check(found.has_value() && found->fileName() == "libgreeter.a");
    check(found && eacp::Files::readFile(*found) == "first");
};

auto tStoreObjects = test("LibraryStore/storesObjectFilesInTheirOrder") = []
{
    auto directory = createEmptyTestDirectory("library-store-objects");
    auto first = directory / "build" / "first" / "unity.cpp.o";
    auto second = directory / "build" / "second" / "unity.cpp.o";
    writeTestFile(first, "first object");
    writeTestFile(second, "second object");
    auto store = lockstep::LibraryStore {directory / "cache"};

    check(store.storeEntry(
        sampleKey,
        {lockstep::CachedTargetKind::objectLibrary, {first, second}, "inputs"}));

    auto entry = store.findEntry(addressOf(sampleKey));
    check(entry.has_value());
    auto files = entry->getFilePaths();
    check(files.size() == 2);
    check(eacp::Files::readFile(files[0]) == "first object");
    check(eacp::Files::readFile(files[1]) == "second object");
    check(store.summarize().entryCounts.at(lockstep::CachedTargetKind::objectLibrary)
          == 1);
};

auto tEntryKind = test("LibraryCachePlan/findsEntryOfTheKindAskedFor") = []
{
    auto directory = createEmptyTestDirectory("library-store-kind");
    auto tool = directory / "build" / "tool";
    writeTestFile(tool, "executable");
    auto store = lockstep::LibraryStore {directory / "cache"};
    store.storeEntry(sampleKey,
                     {lockstep::CachedTargetKind::executable, {tool}, "inputs"});
    auto key = lockstep::createLibraryKey("inputs", {});
    key.key = sampleKey;

    check(lockstep::chooseStoredBuild(
              store, key, lockstep::CachedTargetKind::executable, {})
              .entry.has_value());
    check(!lockstep::chooseStoredBuild(
               store, key, lockstep::CachedTargetKind::staticLibrary, {})
               .entry);
};

auto tStoreCounts = test("LibraryStore/addsStatisticsAcrossConfiguresAndLinks") = []
{
    auto directory = createEmptyTestDirectory("library-store-counts");
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, "archive");
    auto store = lockstep::LibraryStore {directory / "cache"};

    storeLibrary(store, sampleKey, library, "inputs");
    store.addToStatistics(countStaticLibraries({.hits = 1, .misses = 2}));
    store.addToStatistics(countStaticLibraries({.hits = 3}));
    store.addToStatistics(countStaticLibraries({.stores = 1}));

    auto summary = store.summarize();
    check(countStoredLibraries(summary) == 1);
    check(summary.byteCount > 0);
    check(summary.statistics.staticLibraries.hits == 4);
    check(summary.statistics.staticLibraries.misses == 2);
    check(summary.statistics.staticLibraries.stores == 1);
};

auto tConcurrentCounts = test("LibraryStore/countsTwoProcessesAtOnce") = []
{
    auto directory = createEmptyTestDirectory("library-store-concurrent-counts");
    auto writerArguments = eacp::Vector<std::string> {
        std::string {statisticsWriterArgument}, (directory / "cache").str()};
    auto first = eacp::Processes::Process {testExecutablePath, writerArguments};
    auto second = eacp::Processes::Process {testExecutablePath, writerArguments};

    check(first.launched() && second.launched());
    check(first.wait() == 0);
    check(second.wait() == 0);

    auto statistics = lockstep::LibraryStore {directory / "cache"}
                          .summarize()
                          .statistics.staticLibraries;
    check(statistics.hits == 2 * statisticsWritesPerProcess);
    check(statistics.misses == 2 * statisticsWritesPerProcess);
    check(statistics.stores == 2 * statisticsWritesPerProcess);
};

auto tCorruptFiles = test("LibraryStore/readsCorruptOrHalfWrittenFilesAsEmpty") = []
{
    auto directory = createEmptyTestDirectory("library-store-corrupt");
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, "archive");
    auto store = lockstep::LibraryStore {directory / "cache"};
    storeLibrary(store, sampleKey, library, "inputs");
    store.addToStatistics(countStaticLibraries({.hits = 5}));
    auto storeDirectory = getCurrentFormatStore(directory / "cache");

    writeTestFile(storeDirectory / sampleKey / plainBuildId / "manifest.json",
                  "{\"fileName\": \"libgree");
    writeTestFile(storeDirectory / "statistics.json", "not json at all");

    check(!findLibrary(store, sampleKey));
    check(countStoredLibraries(store.summarize()) == 0);
    check(store.summarize().statistics.staticLibraries.hits == 0);

    store.addToStatistics(countStaticLibraries({.hits = 1}));
    check(store.summarize().statistics.staticLibraries.hits == 1);
};

auto tOlderFormat = test("LibraryStore/ignoresEntriesOfAnOlderFormat") = []
{
    auto directory = createEmptyTestDirectory("library-store-older-format");
    auto olderEntry = directory / "cache" / "lib" / sampleKey;
    writeTestFile(olderEntry / "libgreeter.a", "archive");
    writeTestFile(olderEntry / "manifest.json",
                  R"({"fileName": "libgreeter.a", "inputs": "inputs"})");
    writeTestFile(directory / "cache" / "lib" / "statistics.json",
                  R"({"hits": 7, "misses": 9})");
    auto store = lockstep::LibraryStore {directory / "cache"};

    check(!findLibrary(store, sampleKey));
    auto summary = store.summarize();
    check(countStoredLibraries(summary) == 0);
    check(summary.statistics.staticLibraries.hits == 0
          && summary.statistics.staticLibraries.misses == 0);

    store.clear();
    check(!eacp::Files::isDirectory(directory / "cache" / "lib"));
};

auto tStoreClear = test("LibraryStore/clearKeepsPackageSources") = []
{
    auto directory = createEmptyTestDirectory("library-store-clear");
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, "archive");
    auto packageSource = directory / "cache" / "src" / "1111" / "CMakeLists.txt";
    writeTestFile(packageSource, "project(greeter)");
    auto store = lockstep::LibraryStore {directory / "cache"};

    storeLibrary(store, sampleKey, library, "inputs");
    store.addToStatistics(
        countStaticLibraries({.hits = 1, .misses = 1, .stores = 1}));
    store.clear();

    auto summary = store.summarize();
    check(countStoredLibraries(summary) == 0);
    check(summary.statistics.staticLibraries.hits == 0
          && summary.statistics.staticLibraries.misses == 0
          && summary.statistics.staticLibraries.stores == 0);
    check(!eacp::Files::isDirectory(directory / "cache" / "lib"));
    check(eacp::Files::isFile(packageSource));
};

lockstep::LibraryCacheAtLocation
    createCacheAtDirectory(const eacp::FilePath& directory)
{
    return {*lockstep::parseLibraryCacheLocation(directory.str()),
            eacp::FilePath {"cmake"}};
}

lockstep::LibraryStore openStoreAtDirectory(const eacp::FilePath& directory)
{
    return lockstep::LibraryStore {lockstep::LibraryStoreDirectory {directory}};
}

struct SampleEntry
{
    std::string inputs;
    std::string key;
};

SampleEntry createSampleEntry(std::string_view inputs)
{
    return {std::string {inputs}, lockstep::computeSha256Hex(inputs)};
}

auto tFetchesFromDirectory =
    test("LibraryCacheLocation/fetchesFromDirectoryAndNamesWhatItLacks") = []
{
    auto directory = createEmptyTestDirectory("library-cache-location-fetch");
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, "archive");
    auto shared = createSampleEntry("shared inputs");
    auto copiedOnly = createSampleEntry("copied inputs");
    auto missingEverywhere = createSampleEntry("missing inputs");
    storeLibrary(openStoreAtDirectory(directory / "location"),
                 shared.key,
                 library,
                 shared.inputs);
    auto local = lockstep::LibraryStore {directory / "cache"};
    storeLibrary(local, copiedOnly.key, library, copiedOnly.inputs);

    auto fetched = createCacheAtDirectory(directory / "location")
                       .fetchLibraries({{addressOf(shared.key),
                                         addressOf(missingEverywhere.key)},
                                        false,
                                        {addressOf(copiedOnly.key)}},
                                       local);

    check(fetched.fetched == std::set {addressOf(shared.key)});
    check(fetched.locationLacks == std::vector {addressOf(copiedOnly.key)});
    check(fetched.discarded.empty());
    auto copy = findLibrary(local, shared.key);
    check(copy && eacp::Files::readFile(*copy) == "archive");
};

auto tDiscardsChanged =
    test("LibraryCacheLocation/discardsLibraryThatDoesNotMatchItsManifest") = []
{
    auto directory = createEmptyTestDirectory("library-cache-location-discard");
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, "archive");
    auto entry = createSampleEntry("inputs");
    auto location = openStoreAtDirectory(directory / "location");
    storeLibrary(location, entry.key, library, entry.inputs);
    writeTestFile(*findLibrary(location, entry.key), "changed after it was stored");
    auto local = lockstep::LibraryStore {directory / "cache"};

    auto fetched = createCacheAtDirectory(directory / "location")
                       .fetchLibraries({{addressOf(entry.key)}, false, {}}, local);

    check(fetched.fetched.empty());
    check(fetched.discarded.size() == 1
          && fetched.discarded.front().reason.find("SHA-256") != std::string::npos);
    check(!findLibrary(local, entry.key));
};

auto tFetchesObjects =
    test("LibraryCacheLocation/fetchesObjectFilesAndDiscardsAChangedOne") = []
{
    auto directory = createEmptyTestDirectory("library-cache-location-objects");
    auto first = directory / "build" / "first.cpp.o";
    auto second = directory / "build" / "second.cpp.o";
    writeTestFile(first, "first object");
    writeTestFile(second, "second object");
    auto kept = createSampleEntry("kept inputs");
    auto changed = createSampleEntry("changed inputs");
    auto location = openStoreAtDirectory(directory / "location");

    for (const auto& entry: {kept, changed})
        location.storeEntry(entry.key,
                            {lockstep::CachedTargetKind::objectLibrary,
                             {first, second},
                             entry.inputs});

    writeTestFile(location.findEntry(addressOf(changed.key))->getFilePaths().back(),
                  "changed");
    auto local = lockstep::LibraryStore {directory / "cache"};

    auto fetched =
        createCacheAtDirectory(directory / "location")
            .fetchLibraries(
                {{addressOf(kept.key), addressOf(changed.key)}, false, {}}, local);

    check(fetched.fetched == std::set {addressOf(kept.key)});
    check(fetched.discarded.size() == 1
          && fetched.discarded.front().address == addressOf(changed.key));
    auto copy = local.findEntry(addressOf(kept.key));
    check(copy && copy->manifest.kind == lockstep::CachedTargetKind::objectLibrary);
    check(copy
          && eacp::Files::readFile(copy->getFilePaths().back()) == "second object");
};

auto tSendsToDirectory = test("LibraryCacheLocation/sendsToDirectory") = []
{
    auto directory = createEmptyTestDirectory("library-cache-location-send");
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, "archive");
    auto entry = createSampleEntry("inputs");
    auto local = lockstep::LibraryStore {directory / "cache"};
    storeLibrary(local, entry.key, library, entry.inputs);
    auto cache = createCacheAtDirectory(directory / "location");

    cache.sendLibraries({addressOf(entry.key)}, local, {unlimitedBytes});
    cache.sendLibraries({addressOf(entry.key)}, local, {unlimitedBytes});

    auto sent =
        openStoreAtDirectory(directory / "location").findEntry(addressOf(entry.key));
    check(sent && sent->manifest.inputs == entry.inputs);
    check(sent
          && sent->manifest.files.front().sha256
                 == lockstep::computeFileSha256Hex(library));
};

auto tConcurrentSends =
    test("LibraryCacheLocation/twoProcessesSendOneKeyAtOnce") = []
{
    auto directory = createEmptyTestDirectory("library-cache-location-concurrent");
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, "archive");
    auto entry = createSampleEntry("inputs");
    storeLibrary(lockstep::LibraryStore {directory / "cache"},

                 entry.key,
                 library,
                 entry.inputs);
    auto senderArguments =
        eacp::Vector<std::string> {std::string {librarySenderArgument},
                                   (directory / "cache").str(),
                                   (directory / "location").str(),
                                   entry.key,
                                   std::string {sizeNoTestReaches}};
    auto first = eacp::Processes::Process {testExecutablePath, senderArguments};
    auto second = eacp::Processes::Process {testExecutablePath, senderArguments};

    check(first.launched() && second.launched());
    check(first.wait() == 0);
    check(second.wait() == 0);

    auto location = openStoreAtDirectory(directory / "location");
    check(findLibrary(location, entry.key).has_value());
    auto isLeftOver = [](const eacp::Files::DirectoryEntry& entry)
    {
        auto name = entry.path.fileName();
        return name != "used" && name.size() != lockstep::libraryKeyLength;
    };
    auto everyEntry = eacp::Files::DirectoryOptions {};
    everyEntry.includeHidden = true;
    check(std::ranges::none_of(
        eacp::Files::listDirectory(location.getFormatDirectory(), everyEntry),
        isLeftOver));
    check(location.listBuilds(entry.key).size() == 1);
};

auto tClearDuringSend =
    test("LibraryCacheLocation/clearDeletesEverythingWhileSendIsArchiving") = []
{
    auto directory = createEmptyTestDirectory("library-cache-clear-during-send");
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, "archive");
    auto entry = createSampleEntry("inputs");
    auto local = lockstep::LibraryStore {directory / "cache"};
    storeLibrary(local, entry.key, library, entry.inputs);
    auto testExecutable = eacp::FilePath {testExecutablePath};
    auto fakeCMake = directory / "fake-cmake" / testExecutable.fileName();
    eacp::Files::createDirectories(fakeCMake.parentDirectory());
    check(eacp::Files::copyFile(testExecutable, fakeCMake));
    auto location = *lockstep::parseLibraryCacheLocation(
        "lockstep-test-host-never-reached:libraries");

    auto send = [&]
    {
        try
        {
            lockstep::LibraryCacheAtLocation {location, fakeCMake}.sendLibraries(
                {addressOf(entry.key)}, local, {unlimitedBytes});
        }
        catch (const std::exception&)
        {
        }
    };
    auto sender = std::thread {send};
    auto isArchiving = waitForFile(fakeCMake.sibling(fakeArchiverStartedName));
    auto clearFailure = std::string {};

    try
    {
        local.clear();
    }
    catch (const std::exception& error)
    {
        clearFailure = error.what();
    }

    eacp::Files::writeFile(fakeCMake.sibling(fakeArchiverReleaseName), "");
    sender.join();

    check(isArchiving);
    check(clearFailure.empty());
    check(!eacp::Files::isDirectory(directory / "cache" / "lib"));
    auto archiverDirectory =
        eacp::Files::readFile(fakeCMake.sibling(fakeArchiverStartedName));
    check(archiverDirectory.find(directory.fileName()) == std::string::npos);
};

#if defined(_WIN32)
auto tClearKeepsHeldEntry =
    test("LibraryStore/clearKeepsEntryWholeAndNamesItWhenAFileInItIsOpen") = []
{
    auto directory = createEmptyTestDirectory("library-store-clear-held");
    auto library = directory / "build" / "greeter.lib";
    writeTestFile(library, "archive");
    auto held = createSampleEntry("held inputs");
    auto notHeld = createSampleEntry("free inputs");
    auto store = lockstep::LibraryStore {directory / "cache"};
    storeLibrary(store, held.key, library, held.inputs);
    storeLibrary(store, notHeld.key, library, notHeld.inputs);
    auto clearFailure = std::string {};

    {
        auto openLibrary =
            std::ifstream {eacp::toStdPath(*findLibrary(store, held.key))};

        try
        {
            store.clear();
        }
        catch (const std::exception& error)
        {
            clearFailure = error.what();
        }
    }

    check(clearFailure.find(held.key) != std::string::npos);
    check(clearFailure.find(notHeld.key) == std::string::npos);
    check(findLibrary(store, held.key).has_value());
    check(!findLibrary(store, notHeld.key));
    store.clear();
    check(!eacp::Files::isDirectory(directory / "cache" / "lib"));
};
#endif

int runFakeArchiver()
{
    auto directory = eacp::Files::executablePath().parentDirectory();
    auto started = directory / fakeArchiverStartedName;
    auto written = started.sibling(std::string {fakeArchiverStartedName} + ".part");
    eacp::Files::writeFile(written, std::filesystem::current_path().string());
    eacp::Files::moveFile(written, started);
    waitForFile(directory / fakeArchiverReleaseName);
    return 1;
}

int runLibrarySender(const LibrarySenderArguments& arguments)
{
    auto local = lockstep::LibraryStore {eacp::FilePath {arguments.cacheDirectory}};
    auto location = lockstep::LibraryCacheAtLocation {
        *lockstep::parseLibraryCacheLocation(
            std::string {arguments.locationDirectory}),
        eacp::FilePath {"cmake"},
        testExecutablePath};
    location.sendLibraries({addressOf(std::string {arguments.key})},
                           local,
                           {lockstep::parseByteSize(arguments.limitBytes)});
    return 0;
}

int runStatisticsWriter(std::string_view cacheDirectory)
{
    auto store = lockstep::LibraryStore {eacp::FilePath {cacheDirectory}};

    for (auto write = 0; write < statisticsWritesPerProcess; ++write)
        store.addToStatistics(
            countStaticLibraries({.hits = 1, .misses = 1, .stores = 1}));

    return 0;
}
