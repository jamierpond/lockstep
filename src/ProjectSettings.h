#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Reflect.h>

#include <map>
#include <optional>
#include <string>

namespace lockstep
{
constexpr auto projectSettingsFileName = "lockstep.json";

// lockstep.json's "libraryCache": each key it leaves out is unset.
struct LibraryCacheFileSettings
{
    Miro::Omittable<bool> enabled;
    Miro::Omittable<std::string> location;
    Miro::Omittable<std::string> compilerRange;
    Miro::Omittable<std::string> sdkRange;
    Miro::Omittable<std::string> maxSize;
    Miro::Omittable<std::string> locationMaxSize;

    MIRO_REFLECT(
        enabled, location, compilerRange, sdkRange, maxSize, locationMaxSize)
};

// lockstep.json, the project's checked-in settings beside its lockstep.lock.
// Package declarations can join "libraryCache" here later.
struct ProjectSettings
{
    Miro::Omittable<LibraryCacheFileSettings> libraryCache;

    MIRO_REFLECT(libraryCache)
};

// lockstep.json in the directory of the lock file in use, so a
// LOCKSTEP_LOCK_FILE elsewhere brings its own lockstep.json.
eacp::FilePath getProjectSettingsPath(const eacp::FilePath& lockFile);

// Empty settings when the file does not exist. Throws std::runtime_error naming
// the file and the key when it is not JSON, holds a key lockstep does not know,
// or holds a value of the wrong type.
ProjectSettings readProjectSettings(const eacp::FilePath& path);

// Each library cache setting the file sets, by its variable name, as text:
// "enabled": true is ON.
std::map<std::string, std::string>
    getLibraryCacheSettingValues(const ProjectSettings& settings);
} // namespace lockstep
