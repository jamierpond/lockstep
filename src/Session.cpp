#include "Session.h"

#include <eacp/Core/Utils/Files.h>

#include <Miro/Json.h>

namespace lockstep
{
std::optional<SessionPackage> Session::findPackage(const std::string& name) const
{
    for (const auto& package: packages)
    {
        if (package.name == name)
            return package;
    }

    return std::nullopt;
}

std::optional<std::string> Session::findOverride(const std::string& name) const
{
    for (const auto& entry: overrides)
    {
        if (entry.name == name)
            return entry.sourceDirectory;
    }

    return std::nullopt;
}

eacp::FilePath getSessionPath(const eacp::FilePath& buildDirectory)
{
    return buildDirectory / "_lockstep" / "session.json";
}

Session readSession(const eacp::FilePath& buildDirectory)
{
    auto path = getSessionPath(buildDirectory);

    if (!eacp::Files::isFile(path))
        return {};

    return Miro::createFromJSONString<Session>(eacp::Files::readFile(path));
}

void writeSession(const eacp::FilePath& buildDirectory, const Session& session)
{
    auto path = getSessionPath(buildDirectory);
    eacp::Files::createDirectories(path.parentDirectory());
    eacp::Files::writeFile(path, std::string_view {Miro::toJSONString(session)});
}
} // namespace lockstep
