#include <ExecutableCache.h>
#include <LibraryCache.h>

#include <NanoTest/NanoTest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace nano;

namespace
{
constexpr auto packageHash = "1111";
constexpr auto packageDirectory = "/cache/src/1111";
constexpr auto projectDirectory = "/project";

// A tool a package builds, linking the package's greeter library.
struct SampleExecutable
{
    std::string sourceDirectory = packageDirectory;
    std::string compilerId = "GNU";
    std::string buildType = "Release";
    std::string linkerVersion = "2.44";
    std::string executableLinkerFlags;
    std::string linkOptions;
    std::string linkedTargets = "greeter";
    std::string greeterDefinitions = "GREETING=1";
    std::string extraRecords;
};

std::string createExecutableRecords(const SampleExecutable& tool)
{
    return "global\t-\tENABLED_LANGUAGES\tCXX\n"
           "directory\t/cache/src/1111\tCMAKE_CXX_COMPILER_ID\t"
           + tool.compilerId
           + "\n"
             "directory\t/cache/src/1111\tCMAKE_BUILD_TYPE\t"
           + tool.buildType
           + "\n"
             "directory\t/cache/src/1111\tCMAKE_CXX_COMPILER_LINKER_VERSION\t"
           + tool.linkerVersion
           + "\n"
             "directory\t/cache/src/1111\tCMAKE_EXE_LINKER_FLAGS\t"
           + tool.executableLinkerFlags
           + "\n"
             "target\ttool\tTYPE\tEXECUTABLE\n"
             "target\ttool\tSOURCE_DIR\t"
           + tool.sourceDirectory
           + "\n"
             "target\ttool\tSTUB_LANGUAGE\tCXX\n"
             "target\ttool\tCOMPILED_LANGUAGES\tCXX\n"
             "target\ttool\tLINK_OPTIONS\t"
           + tool.linkOptions
           + "\n"
             "target\ttool\tLINKED_TARGETS\t"
           + tool.linkedTargets
           + "\n"
             "target\tgreeter\tTYPE\tSTATIC_LIBRARY\n"
             "target\tgreeter\tSOURCE_DIR\t/cache/src/1111\n"
             "target\tgreeter\tSTUB_LANGUAGE\tCXX\n"
             "target\tgreeter\tCOMPILED_LANGUAGES\tCXX\n"
             "target\tgreeter\tCOMPILE_DEFINITIONS\t"
           + tool.greeterDefinitions + "\n" + tool.extraRecords;
}

struct ExecutableCheck
{
    std::optional<std::string> reason;
    std::string key;
    std::string buildId;
    std::string greeterKey;
};

ExecutableCheck checkExecutable(const SampleExecutable& tool,
                                const std::string& buildDirectory = "/build")
{
    auto description =
        lockstep::parseConfigureDescription(createExecutableRecords(tool));
    auto packages = std::vector<lockstep::ConfiguredPackage> {
        {"greeter", packageHash, packageDirectory}, {"local", "", projectDirectory}};
    auto names = lockstep::PackageVariableNames {};
    auto inputs = lockstep::LibraryCacheInputs {
        description, packages, names, {buildDirectory, "/cache"}};
    auto libraryKeys = lockstep::LinkedLibraryKeys {};
    auto greeterKey = std::string {};

    if (!lockstep::findReasonNotToCache(inputs, "greeter"))
    {
        auto greeter = lockstep::computeLibraryKey(inputs, "greeter");
        greeterKey = greeter.key;
        libraryKeys["greeter"] = {greeter.key, greeter.buildId};
    }

    auto key = lockstep::computeExecutableKey(inputs, "tool", libraryKeys);
    return {lockstep::findReasonNotToCacheExecutable(inputs, "tool", libraryKeys),
            key.key,
            key.buildId,
            greeterKey};
}

bool isSkippedFor(const SampleExecutable& tool, std::string_view words)
{
    auto reason = checkExecutable(tool).reason;
    return reason.has_value() && reason->find(words) != std::string::npos;
}
} // namespace

auto tCachesExecutable = test("ExecutableCache/cachesToolOfLockedPackage") = []
{
    auto checked = checkExecutable({});
    check(!checked.reason);
    check(!checked.greeterKey.empty());
};

auto tExecutableKeyIgnoresBuildDirectory =
    test("ExecutableCache/keyIgnoresBuildDirectory") = []
{
    check(checkExecutable({}, "/build").key
          == checkExecutable({}, "/other-build").key);
};

auto tExecutableKeyFollowsLibrary =
    test("ExecutableCache/keyChangesWithLibraryItLinks") = []
{
    auto changed = SampleExecutable {};
    changed.greeterDefinitions = "GREETING=2";
    check(checkExecutable({}).key != checkExecutable(changed).key);
    check(checkExecutable({}).greeterKey != checkExecutable(changed).greeterKey);
};

auto tExecutableKeyFollowsLink =
    test("ExecutableCache/keyChangesWithLinkSettings") = []
{
    auto withLinkOptions = SampleExecutable {};
    withLinkOptions.linkOptions = "-Wl,--gc-sections";
    auto withLinkerFlags = SampleExecutable {};
    withLinkerFlags.executableLinkerFlags = "-static";
    auto otherLinker = SampleExecutable {};
    otherLinker.linkerVersion = "2.45";
    auto plain = checkExecutable({});

    check(plain.key != checkExecutable(withLinkOptions).key);
    check(plain.key != checkExecutable(withLinkerFlags).key);
    check(plain.key == checkExecutable(otherLinker).key);
    check(plain.buildId != checkExecutable(otherLinker).buildId);
};

auto tLibraryKeyIgnoresLink = test("LibraryCache/keyIgnoresLinkSettings") = []
{
    auto otherLink = SampleExecutable {};
    otherLink.executableLinkerFlags = "-static";
    otherLink.linkerVersion = "2.45";
    otherLink.extraRecords = "target\tgreeter\tINTERFACE_LINK_OPTIONS\t-Wl,-z,now\n";
    check(checkExecutable({}).greeterKey == checkExecutable(otherLink).greeterKey);
};

auto tSkipsProjectExecutable =
    test("ExecutableCache/skipsExecutableOutsideLockedPackages") = []
{
    auto projectTool = SampleExecutable {};
    projectTool.sourceDirectory = projectDirectory;
    check(isSkippedFor(projectTool, "not a locked package"));
};

auto tSkipsBundleAndExports =
    test("ExecutableCache/skipsAppBundleAndExecutableExportingSymbols") = []
{
    auto bundle = SampleExecutable {};
    bundle.extraRecords = "target\ttool\tMACOSX_BUNDLE\tON\n";
    auto exporting = SampleExecutable {};
    exporting.extraRecords = "target\ttool\tENABLE_EXPORTS\tON\n";
    check(isSkippedFor(bundle, "app bundle"));
    check(isSkippedFor(exporting, "import library"));
};

auto tSkipsSharedLibrary =
    test("ExecutableCache/skipsExecutableLinkingSharedLibraryOfThisBuild") = []
{
    auto linkingShared = SampleExecutable {};
    linkingShared.linkedTargets = "greeter;helper";
    linkingShared.extraRecords =
        "target\thelper\tTYPE\tSHARED_LIBRARY\n"
        "target\thelper\tSOURCE_DIR\t/cache/src/1111/helper\n";
    check(isSkippedFor(linkingShared, "shared library"));
};

auto tSkipsSkippedLibrary =
    test("ExecutableCache/skipsExecutableLinkingLibraryTheCacheSkips") = []
{
    auto linkingSkipped = SampleExecutable {};
    linkingSkipped.extraRecords =
        "target\tgreeter\tGENERATOR_EXPRESSION_SOURCE\t$<$<CONFIG:Debug>:debug.cpp>\n";
    check(isSkippedFor(linkingSkipped, "greeter, which the cache skips"));
};

auto tSkipsLinkerProgramDatabase =
    test("ExecutableCache/skipsMsvcExecutableLinkedWithDebug") = []
{
    auto withLinkerFlags = [](const std::string& flags)
    {
        auto tool = SampleExecutable {};
        tool.compilerId = "MSVC";
        tool.executableLinkerFlags = flags;
        return tool;
    };

    check(isSkippedFor(withLinkerFlags("/machine:x64 /debug /INCREMENTAL"), ".pdb"));
    check(!checkExecutable(withLinkerFlags("/machine:x64 /DEBUG:NONE")).reason);
    check(!checkExecutable(withLinkerFlags("/machine:x64")).reason);
};

auto tSkipsBuildDirectoryInLink =
    test("ExecutableCache/skipsExecutableWhoseLinkNamesTheBuildDirectory") = []
{
    auto withRpath = [](const std::string& rpath)
    {
        auto tool = SampleExecutable {};
        tool.extraRecords = "target\ttool\tBUILD_RPATH\t" + rpath + "\n";
        return tool;
    };
    auto linkingImported = SampleExecutable {};
    linkingImported.linkedTargets = "greeter;Zlib::Zlib";
    linkingImported.extraRecords =
        "target\tZlib::Zlib\tIMPORTED\tTRUE\n"
        "target\tZlib::Zlib\tIMPORTED_LOCATION_RELEASE\t/build/zlib/libz.a\n";

    check(isSkippedFor(withRpath("/build/lib"), "BUILD_RPATH of tool"));
    check(!checkExecutable(withRpath("/build-elsewhere/lib")).reason);
    check(isSkippedFor(linkingImported, "IMPORTED_LOCATION_RELEASE of Zlib::Zlib"));
};

auto tSkipsNotVisibleTarget =
    test("ExecutableCache/skipsExecutableLinkingTargetTheTopLevelCannotSee") = []
{
    auto tool = SampleExecutable {};
    tool.extraRecords =
        "target\tgreeter\tLINKED_NAMES_NOT_VISIBLE_INTERFACE_LINKED_TARGETS\t"
        "Threads::Threads\n";
    check(isSkippedFor(tool, "Threads::Threads"));
};

auto tSkipsUnknownLinker =
    test("ExecutableCache/skipsExecutableWhenCMakeReportsNoLinkerVersion") = []
{
    auto tool = SampleExecutable {};
    tool.linkerVersion = "";
    check(isSkippedFor(tool, "CMAKE_CXX_COMPILER_LINKER_VERSION"));
};
