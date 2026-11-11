#include "LibrarySender.h"
#include "StatisticsWriter.h"
#include "TestDirectory.h"

#include <LibraryCache.h>
#include <LibraryCacheLocation.h>
#include <LibraryStore.h>
#include <Sha256.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Process/Process.h>

#include <string>
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
           + build.appleSdkBuildVersion + "\n";
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

std::string computeKey(const SampleBuild& build)
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
    return lockstep::computeLibraryKey(inputs, "greeter").key;
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
    check(computeKey(SampleBuild {}) == computeKey(second));
};

auto tIgnoresDirectoryIds = test("LibraryCache/keyIgnoresDirectoryIds") = []
{
    auto first = SampleBuild {};
    first.linkLibraries = "::@(0x55d0c0ffee);fmt;::@";
    auto second = first;
    second.linkLibraries = "::@(0x7f00deadbeef);fmt;::@";
    check(computeKey(first) == computeKey(second));
};

auto tIgnoresTryCompileNames =
    test("LibraryCache/keyIgnoresTryCompileProjectNames") = []
{
    auto first = SampleBuild {};
    first.loudValue = "ninja -v cmTC_bdb97\nCMakeFiles/cmTC_bdb97.dir/main.c.o";
    auto second = first;
    second.loudValue = "ninja -v cmTC_84289\nCMakeFiles/cmTC_84289.dir/main.c.o";
    check(computeKey(first) == computeKey(second));
};

auto tDefinitionMisses = test("LibraryCache/keyChangesWithCompileDefinition") = []
{
    auto changed = SampleBuild {};
    changed.definitions = "GREETING=2";
    check(computeKey(SampleBuild {}) != computeKey(changed));
};

auto tVariableMisses =
    test("LibraryCache/keyChangesWithVariableThePackageNames") = []
{
    auto changed = SampleBuild {};
    changed.loudValue = "ON";
    check(computeKey(SampleBuild {}) != computeKey(changed));
};

auto tIgnoresCallText = test("LibraryCache/keyIgnoresTextOfAddPackageCall") = []
{
    auto otherCall = SampleBuild {};
    otherCall.callArguments =
        "GIT_TAG;0123456789abcdef0123456789abcdef01234567;NAME;greeter;"
        "OPTIONS;GREETER_LOUD OFF;SYSTEM;YES";
    otherCall.callArgumentCount = "8";
    otherCall.firstCallArgument = "GIT_TAG";
    check(computeKey(SampleBuild {}) == computeKey(otherCall));
};

auto tOptionInCallMisses =
    test("LibraryCache/keyChangesWithOptionValueOfAddPackageCall") = []
{
    auto loud = SampleBuild {};
    loud.callArguments = "NAME;greeter;GIT_TAG;develop;OPTIONS;GREETER_LOUD ON";
    loud.loudValue = "ON";
    check(computeKey(SampleBuild {}) != computeKey(loud));
};

auto tIgnoresGenerator =
    test("LibraryCache/keyIgnoresGeneratorWithoutModuleScanning") = []
{
    auto ninja = SampleBuild {};
    ninja.generator = "Ninja";
    check(computeKey(SampleBuild {}) == computeKey(ninja));
};

auto tScanningGeneratorMisses =
    test("LibraryCache/keyChangesWithGeneratorWhenScanningForModules") = []
{
    auto scanned = SampleBuild {};
    scanned.scanForModules = "";
    auto scannedNinja = scanned;
    scannedNinja.generator = "Ninja";
    check(computeKey(scanned) != computeKey(scannedNinja));
};

auto tIgnoresSystem = test("LibraryCache/keyIgnoresSystemProperty") = []
{
    auto system = SampleBuild {};
    system.system = "ON";
    check(computeKey(SampleBuild {}) == computeKey(system));
};

auto tIgnoresStandardRequired =
    test("LibraryCache/keyIgnoresCxxStandardRequired") = []
{
    auto notRequired = SampleBuild {};
    notRequired.cxxStandardRequired = "OFF";
    check(computeKey(SampleBuild {}) == computeKey(notRequired));
};

auto tIgnoresOtherLanguage =
    test("LibraryCache/keyIgnoresSettingsOfLanguageTargetDoesNotCompile") = []
{
    auto otherC = SampleBuild {};
    otherC.cCompilerVersion = "14.1.0";
    otherC.cFlags = "-O3 -DOTHER";
    check(computeKey(SampleBuild {}) == computeKey(otherC));
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
    check(computeKey(withC) != computeKey(otherC));
    check(computeKey(withC) != computeKey(otherFlags));
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
    check(computeKey(withC) == computeKey(otherTopLevel));
    check(computeKey(withC) != computeKey(otherPackage));
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

    check(computeKey(apple) != computeKey(otherSdkVersion));
    check(computeKey(apple) != computeKey(otherSdkBuild));
    check(computeKey(SampleBuild {}) == computeKey(notApple));
};

auto tCacheDirectory = test("LibraryCache/keyChangesWithCacheDirectory") = []
{
    auto otherCache = SampleBuild {};
    otherCache.cacheDirectory = "/other-cache";
    check(computeKey(SampleBuild {}) != computeKey(otherCache));
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

        return computeKey(withCache("/cache"))
               == computeKey(withCache("/other-cache"));
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
    check(computeKey(cxxBuild) != computeKey(cxxBuildInOtherCache));
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
        findReason("target\tgreeter\tSOURCE_DIR\t/cache/src/1111\n"
                   "target\tgreeter\tSTUB_LANGUAGE\tCXX\n"
                   "target\tapp\tSOURCES\tmain.cpp;$<TARGET_OBJECTS:greeter>\n");
    check(reason.has_value() && reason->find("TARGET_OBJECTS") != std::string::npos);
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

    check(!store.findLibrary("key"));
    check(store.storeLibrary("key", library, "inputs"));
    writeTestFile(library, "second");
    check(!store.storeLibrary("key", library, "inputs"));

    auto found = store.findLibrary("key");
    check(found.has_value() && found->fileName() == "libgreeter.a");
    check(found && eacp::Files::readFile(*found) == "first");
};

auto tStoreCounts = test("LibraryStore/addsStatisticsAcrossConfiguresAndLinks") = []
{
    auto directory = createEmptyTestDirectory("library-store-counts");
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, "archive");
    auto store = lockstep::LibraryStore {directory / "cache"};

    store.storeLibrary(sampleKey, library, "inputs");
    store.addToStatistics({.hits = 1, .misses = 2});
    store.addToStatistics({.hits = 3});
    store.addToStatistics({.stores = 1});

    auto summary = store.summarize();
    check(summary.libraryCount == 1);
    check(summary.byteCount > 0);
    check(summary.statistics.hits == 4);
    check(summary.statistics.misses == 2);
    check(summary.statistics.stores == 1);
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

    auto statistics =
        lockstep::LibraryStore {directory / "cache"}.summarize().statistics;
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
    store.storeLibrary(sampleKey, library, "inputs");
    store.addToStatistics({.hits = 5});
    auto storeDirectory = getCurrentFormatStore(directory / "cache");

    writeTestFile(storeDirectory / sampleKey / "manifest.json",
                  "{\"fileName\": \"libgree");
    writeTestFile(storeDirectory / "statistics.json", "not json at all");

    check(!store.findLibrary(sampleKey));
    check(store.summarize().libraryCount == 0);
    check(store.summarize().statistics.hits == 0);

    store.addToStatistics({.hits = 1});
    check(store.summarize().statistics.hits == 1);
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

    check(!store.findLibrary(sampleKey));
    auto summary = store.summarize();
    check(summary.libraryCount == 0);
    check(summary.statistics.hits == 0 && summary.statistics.misses == 0);

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

    store.storeLibrary(sampleKey, library, "inputs");
    store.addToStatistics({.hits = 1, .misses = 1, .stores = 1});
    store.clear();

    auto summary = store.summarize();
    check(summary.libraryCount == 0);
    check(summary.statistics.hits == 0 && summary.statistics.misses == 0
          && summary.statistics.stores == 0);
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

auto tParsesLocation = test("LibraryCacheLocation/parsesDirectoryAndSshForms") = []
{
    using Kind = lockstep::LibraryCacheLocationKind;
    auto ssh = lockstep::parseLibraryCacheLocation("jamie@tamby:.cache/libraries");
    check(ssh && ssh->kind == Kind::ssh);
    check(ssh && ssh->sshHost == "jamie@tamby"
          && ssh->directory == ".cache/libraries");

    auto home = lockstep::parseLibraryCacheLocation("tamby:");
    check(home && home->kind == Kind::ssh && home->directory.empty());

    for (auto directory: {"/mnt/share/libraries", "C:\\share", "C:/share", "share"})
    {
        auto parsed = lockstep::parseLibraryCacheLocation(directory);
        check(parsed && parsed->kind == Kind::directory
              && parsed->directory == directory);
    }

    check(!lockstep::parseLibraryCacheLocation(""));
};

auto tFetchesFromDirectory =
    test("LibraryCacheLocation/fetchesFromDirectoryAndNamesWhatItLacks") = []
{
    auto directory = createEmptyTestDirectory("library-cache-location-fetch");
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, "archive");
    auto shared = createSampleEntry("shared inputs");
    auto copiedOnly = createSampleEntry("copied inputs");
    auto missingEverywhere = createSampleEntry("missing inputs");
    openStoreAtDirectory(directory / "location")
        .storeLibrary(shared.key, library, shared.inputs);
    auto local = lockstep::LibraryStore {directory / "cache"};
    local.storeLibrary(copiedOnly.key, library, copiedOnly.inputs);

    auto fetched =
        createCacheAtDirectory(directory / "location")
            .fetchLibraries({{shared.key, missingEverywhere.key}, {copiedOnly.key}},
                            local);

    check(fetched.fetchedKeys == std::set<std::string> {shared.key});
    check(fetched.keysLocationLacks == std::vector<std::string> {copiedOnly.key});
    check(fetched.discarded.empty());
    auto copy = local.findLibrary(shared.key);
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
    location.storeLibrary(entry.key, library, entry.inputs);
    writeTestFile(*location.findLibrary(entry.key), "changed after it was stored");
    auto local = lockstep::LibraryStore {directory / "cache"};

    auto fetched = createCacheAtDirectory(directory / "location")
                       .fetchLibraries({{entry.key}, {}}, local);

    check(fetched.fetchedKeys.empty());
    check(fetched.discarded.size() == 1
          && fetched.discarded.front().reason.find("SHA-256") != std::string::npos);
    check(!local.findLibrary(entry.key));
};

auto tSendsToDirectory = test("LibraryCacheLocation/sendsToDirectory") = []
{
    auto directory = createEmptyTestDirectory("library-cache-location-send");
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, "archive");
    auto entry = createSampleEntry("inputs");
    auto local = lockstep::LibraryStore {directory / "cache"};
    local.storeLibrary(entry.key, library, entry.inputs);
    auto cache = createCacheAtDirectory(directory / "location");

    cache.sendLibraries({entry.key}, local);
    cache.sendLibraries({entry.key}, local);

    auto sent = openStoreAtDirectory(directory / "location").readManifest(entry.key);
    check(sent && sent->inputs == entry.inputs);
    check(sent && sent->librarySha256 == lockstep::computeFileSha256Hex(library));
};

auto tConcurrentSends =
    test("LibraryCacheLocation/twoProcessesSendOneKeyAtOnce") = []
{
    auto directory = createEmptyTestDirectory("library-cache-location-concurrent");
    auto library = directory / "build" / "libgreeter.a";
    writeTestFile(library, "archive");
    auto entry = createSampleEntry("inputs");
    lockstep::LibraryStore {directory / "cache"}.storeLibrary(
        entry.key, library, entry.inputs);
    auto senderArguments =
        eacp::Vector<std::string> {std::string {librarySenderArgument},
                                   (directory / "cache").str(),
                                   (directory / "location").str(),
                                   entry.key};
    auto first = eacp::Processes::Process {testExecutablePath, senderArguments};
    auto second = eacp::Processes::Process {testExecutablePath, senderArguments};

    check(first.launched() && second.launched());
    check(first.wait() == 0);
    check(second.wait() == 0);

    auto location = openStoreAtDirectory(directory / "location");
    check(location.findLibrary(entry.key).has_value());
    auto entries = eacp::Files::listDirectory(location.getFormatDirectory(), {});
    check(entries.size() == 1);
};

int runLibrarySender(const LibrarySenderArguments& arguments)
{
    auto local = lockstep::LibraryStore {eacp::FilePath {arguments.cacheDirectory}};
    createCacheAtDirectory(eacp::FilePath {arguments.locationDirectory})
        .sendLibraries({std::string {arguments.key}}, local);
    return 0;
}

int runStatisticsWriter(std::string_view cacheDirectory)
{
    auto store = lockstep::LibraryStore {eacp::FilePath {cacheDirectory}};

    for (auto write = 0; write < statisticsWritesPerProcess; ++write)
        store.addToStatistics({.hits = 1, .misses = 1, .stores = 1});

    return 0;
}
