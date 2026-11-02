#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Reflect.h>

#include <optional>
#include <string>
#include <vector>

namespace lockstep
{
// What one configure has added so far, kept in <build>/_lockstep/session.json
// so a package a second subproject asks for is added once, as CPM does.
struct SessionPackage
{
    std::string name;
    std::string requestedRef;
    std::string sourceDirectory;
    std::string binaryDirectory;

    MIRO_REFLECT(name, requestedRef, sourceDirectory, binaryDirectory)
};

struct SessionOverride
{
    std::string name;
    std::string sourceDirectory;
    std::string variable;

    MIRO_REFLECT(name, sourceDirectory, variable)
};

struct Session
{
    std::vector<SessionOverride> overrides;
    std::vector<SessionPackage> packages;

    MIRO_REFLECT(overrides, packages)

    std::optional<SessionPackage> findPackage(const std::string& name) const;
    std::optional<SessionOverride> findOverride(const std::string& name) const;
};

eacp::FilePath getSessionPath(const eacp::FilePath& buildDirectory);
Session readSession(const eacp::FilePath& buildDirectory);
void writeSession(const eacp::FilePath& buildDirectory, const Session& session);
} // namespace lockstep
