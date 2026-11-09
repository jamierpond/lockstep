#include "FileHash.h"
#include "LongPath.h"

#include <eacp/Core/Utils/Sha256.h>
#include <eacp/Core/Utils/Strings.h>

#include <stdexcept>

namespace lockstep
{
std::string computeFileSha256HexOrThrow(const std::filesystem::path& file)
{
    auto path = eacp::FilePath {file};
    auto digest = eacp::computeFileSha256(path);

    if (!digest)
        throw std::runtime_error("cannot open " + path.str() + " to hash it");

    return eacp::Strings::toHex(*digest);
}

std::string computeFileSha256HexOrThrow(const eacp::FilePath& file)
{
    return computeFileSha256HexOrThrow(getLongPathSafeStdPath(file));
}
} // namespace lockstep
