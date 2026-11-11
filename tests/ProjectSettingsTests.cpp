#include "TestDirectory.h"

#include <LibraryCacheSettings.h>
#include <ProjectSettings.h>

#include <NanoTest/NanoTest.h>

#include <stdexcept>
#include <string>

using namespace nano;

namespace ProjectSettingsTests
{
std::string findReadError(std::string_view text)
{
    auto directory = createEmptyTestDirectory("project-settings-error");
    auto path = directory / "lockstep.json";
    writeTestFile(path, text);

    try
    {
        lockstep::readProjectSettings(path);
        return {};
    }
    catch (const std::runtime_error& error)
    {
        return error.what();
    }
}

bool holds(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}
} // namespace ProjectSettingsTests

using namespace ProjectSettingsTests;

auto tReadsSettings = test("ProjectSettings/readsTheLibraryCacheSettings") = []
{
    auto directory = createEmptyTestDirectory("project-settings-read");
    auto path = directory / "lockstep.json";
    writeTestFile(
        path,
        R"({ "libraryCache": { "enabled": true, "location": "user@host:dir",
                       "compilerRange": ">=21.0.0", "sdkRange": ">=26.5",
                       "maxSize": "5 GiB" } })");

    auto values =
        lockstep::getLibraryCacheSettingValues(lockstep::readProjectSettings(path));

    check(values.at("LOCKSTEP_LIBRARY_CACHE") == "ON");
    check(values.at("LOCKSTEP_LIBRARY_CACHE_LOCATION") == "user@host:dir");
    check(values.at("LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE") == ">=21.0.0");
    check(values.at("LOCKSTEP_LIBRARY_CACHE_SDK_RANGE") == ">=26.5");
    check(values.at("LOCKSTEP_LIBRARY_CACHE_MAX_SIZE") == "5 GiB");
    check(!values.contains("LOCKSTEP_LIBRARY_CACHE_LOCATION_MAX_SIZE"));
};

auto tNoFile = test("ProjectSettings/noFileSetsNothing") = []
{
    auto directory = createEmptyTestDirectory("project-settings-none");
    auto settings = lockstep::readProjectSettings(directory / "lockstep.json");
    check(lockstep::getLibraryCacheSettingValues(settings).empty());
    check(lockstep::getLibraryCacheSettingValues(
              lockstep::readProjectSettings(eacp::FilePath {}))
              .empty());
};

auto tBesideLock = test("ProjectSettings/isBesideTheLockFileInUse") = []
{
    check(lockstep::getProjectSettingsPath(eacp::FilePath {"/work/locks/app.lock"})
          == eacp::FilePath {"/work/locks/lockstep.json"});
};

auto tUnknownKey = test("ProjectSettings/refusesAKeyItDoesNotKnowByName") = []
{
    auto topLevel = findReadError(R"({"libraryCash": {}})");
    auto nested =
        findReadError(R"({"libraryCache": {"enabled": true, "maxSise": "1 GiB"}})");

    check(holds(topLevel,
                "lockstep.json: lockstep does not know the key \"libraryCash\""));
    check(holds(topLevel, "it knows libraryCache"));
    check(holds(nested, "does not know the key \"libraryCache.maxSise\""));
    check(holds(nested, "maxSize"));
};

auto tWrongType = test("ProjectSettings/refusesAValueOfTheWrongTypeByName") = []
{
    check(holds(findReadError(R"({"libraryCache": {"enabled": "yes"}})"),
                "\"libraryCache.enabled\" must be true or false"));
    check(holds(findReadError(R"({"libraryCache": {"location": 5}})"),
                "\"libraryCache.location\" must be a string"));
    check(holds(findReadError(R"({"libraryCache": {"maxSize": null}})"),
                "\"libraryCache.maxSize\" must be a string"));
    check(holds(findReadError(R"({"libraryCache": ["enabled"]})"),
                "\"libraryCache\" must be an object"));
    check(holds(findReadError(R"(["libraryCache"])"),
                "the whole file must be an object"));
    check(
        holds(findReadError(R"({"libraryCache": {)"), "lockstep.json is not JSON"));
};

auto tPrecedence =
    test("LibraryCacheSettings/buildDirectoryThenEnvironmentThenFile") = []
{
    auto sources = lockstep::SettingSources {};
    sources.projectFile = {{"LOCKSTEP_LIBRARY_CACHE", "ON"},
                           {"LOCKSTEP_LIBRARY_CACHE_LOCATION", "file-location"},
                           {"LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE", ">=1"}};
    sources.environment = {
        {"LOCKSTEP_LIBRARY_CACHE_LOCATION", "environment-location"},
        {"LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE", ""}};
    sources.cacheVariables = {{"LOCKSTEP_LIBRARY_CACHE_LOCATION", ""}};

    auto settings = lockstep::resolveLibraryCacheSettings(sources);

    check(settings.enabled.text == "ON"
          && settings.enabled.source == lockstep::SettingSource::projectFile);
    check(settings.location.text.empty()
          && settings.location.source == lockstep::SettingSource::buildDirectory);
    check(settings.compilerRange.text == ">=1"
          && settings.compilerRange.source == lockstep::SettingSource::projectFile);
    check(settings.maxSize.text == "5 GiB"
          && settings.maxSize.source == lockstep::SettingSource::defaultValue);

    sources.cacheVariables.clear();
    check(lockstep::resolveLibraryCacheSettings(sources).location.text
          == "environment-location");
};

auto tRangeErrorNamesSource =
    test("LibraryCacheSettings/aBadRangeNamesItsSetting") = []
{
    auto sources = lockstep::SettingSources {};
    sources.projectFile = {{"LOCKSTEP_LIBRARY_CACHE_SDK_RANGE", "26.5"}};
    auto message = std::string {};

    try
    {
        lockstep::parseToolchainRanges(
            lockstep::resolveLibraryCacheSettings(sources));
    }
    catch (const std::runtime_error& error)
    {
        message = error.what();
    }

    check(holds(message,
                "LOCKSTEP_LIBRARY_CACHE_SDK_RANGE from lockstep.json: '26.5'"));
};
