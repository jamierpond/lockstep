#include "ProjectSettings.h"
#include "LibraryCacheSettings.h"

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

#include <Miro/Json.h>

#include <stdexcept>

namespace lockstep
{
namespace
{
// Where in the file a check is: the file, and the key path so far.
struct FilePlace
{
    const eacp::FilePath& file;
    std::string keyPath;

    FilePlace at(const std::string& key) const
    {
        if (keyPath.empty())
            return {file, key};

        return {file, keyPath + "." + key};
    }
};

// The settings with every key set, so their JSON names every key lockstep
// knows and the type of each value.
ProjectSettings createEveryKey()
{
    auto libraryCache = LibraryCacheFileSettings {};
    libraryCache.enabled = false;
    libraryCache.location = std::string {};
    libraryCache.compilerRange = std::string {};
    libraryCache.sdkRange = std::string {};
    libraryCache.maxSize = std::string {};
    libraryCache.locationMaxSize = std::string {};

    auto settings = ProjectSettings {};
    settings.libraryCache = libraryCache;
    return settings;
}

std::string describeType(const Miro::JSON& value)
{
    if (value.isBool())
        return "true or false";

    if (value.isObject())
        return "an object";

    return "a string";
}

std::string listKeys(const Miro::Json::Object& object)
{
    auto keys = eacp::Vector<std::string> {};

    for (const auto& [key, value]: object)
        keys.add(key);

    return eacp::Strings::join(keys, ", ");
}

std::runtime_error createWrongTypeError(const FilePlace& place,
                                        const Miro::JSON& expected)
{
    auto what = "\"" + place.keyPath + "\"";

    if (place.keyPath.empty())
        what = "the whole file";

    return std::runtime_error {place.file.str() + ": " + what + " must be "
                               + describeType(expected)};
}

// The value at `key` in `object`, or null when there is none or `object` is
// not an object.
Miro::JSON findKey(const Miro::JSON& object, const std::string& key)
{
    if (!object.isObject())
        return {};

    if (const auto* value = Miro::Json::find(object.asObject(), key))
        return *value;

    return {};
}

bool hasKey(const Miro::JSON& object, const std::string& key)
{
    return object.isObject() && Miro::Json::find(object.asObject(), key) != nullptr;
}

void checkObject(const FilePlace& place,
                 const Miro::JSON& written,
                 const Miro::JSON& known,
                 const Miro::JSON& read);

// `written` is what the file holds at `place`, `known` the same key with every
// key set, and `read` what lockstep read back, which differs from `written`
// when the value had the wrong type.
void checkValue(const FilePlace& place,
                const Miro::JSON& written,
                const Miro::JSON& known,
                const Miro::JSON& read)
{
    if (known.isObject())
    {
        checkObject(place, written, known, read);
        return;
    }

    if (!(written == read) || written.isNull())
        throw createWrongTypeError(place, known);
}

void checkObject(const FilePlace& place,
                 const Miro::JSON& written,
                 const Miro::JSON& known,
                 const Miro::JSON& read)
{
    if (!written.isObject())
        throw createWrongTypeError(place, known);

    for (const auto& [key, value]: written.asObject())
    {
        if (!hasKey(known, key))
            throw std::runtime_error {place.file.str()
                                      + ": lockstep does not know the key \""
                                      + place.at(key).keyPath + "\"; it knows "
                                      + listKeys(known.asObject())};

        checkValue(place.at(key), value, findKey(known, key), findKey(read, key));
    }
}

Miro::JSON parseFile(const eacp::FilePath& path)
{
    try
    {
        return Miro::Json::parse(eacp::Files::readFile(path));
    }
    catch (const Miro::Json::Error& error)
    {
        throw std::runtime_error {path.str() + " is not JSON: " + error.what()};
    }
}
} // namespace

eacp::FilePath getProjectSettingsPath(const eacp::FilePath& lockFile)
{
    return lockFile.parentDirectory() / projectSettingsFileName;
}

ProjectSettings readProjectSettings(const eacp::FilePath& path)
{
    if (!eacp::Files::isFile(path))
        return {};

    auto written = parseFile(path);
    auto settings = Miro::createFromJSON<ProjectSettings>(written);
    checkObject(FilePlace {path, {}},
                written,
                Miro::toJSON(createEveryKey()),
                Miro::toJSON(settings));
    return settings;
}

std::map<std::string, std::string>
    getLibraryCacheSettingValues(const ProjectSettings& settings)
{
    auto values = std::map<std::string, std::string> {};

    if (!settings.libraryCache)
        return values;

    auto json = Miro::toJSON(*settings.libraryCache);

    for (const auto& description: getSettingDescriptions())
    {
        auto key = std::string {description.projectFileKey};

        if (!hasKey(json, key))
            continue;

        auto value = findKey(json, key);
        auto& text = values[std::string {description.variable}];

        if (value.isBool())
            text = value.asBool() ? "ON" : "OFF";
        else
            text = value.asString();
    }

    return values;
}
} // namespace lockstep
