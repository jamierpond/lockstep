#include "FileSha256.h"

#include "LongPath.h"

#include <eacp/Core/Utils/Strings.h>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace lockstep
{
eacp::Sha256Digest computeFileSha256(const std::filesystem::path& file)
{
    constexpr auto readChunkSize = std::size_t {1 << 20};
    auto stream = std::ifstream {file, std::ios::binary};

    if (!stream)
        throw std::runtime_error("cannot open " + eacp::FilePath {file}.str()
                                 + " to hash it");

    auto hasher = eacp::Sha256 {};
    auto chunk = std::vector<char>(readChunkSize);

    while (stream.read(chunk.data(), static_cast<std::streamsize>(chunk.size()))
           || stream.gcount() > 0)
    {
        auto readCount = static_cast<std::size_t>(stream.gcount());
        hasher.update(std::string_view {chunk.data(), readCount});
    }

    return hasher.finish();
}

eacp::Sha256Digest computeFileSha256(const eacp::FilePath& file)
{
    return computeFileSha256(getLongPathSafeStdPath(file));
}

std::string computeFileSha256Hex(const eacp::FilePath& file)
{
    const auto digest = lockstep::computeFileSha256(file);
    return eacp::Strings::toHex(digest);
}

std::string computeFileSha256Hex(const std::filesystem::path& file)
{
    const auto digest = lockstep::computeFileSha256(file);
    return eacp::Strings::toHex(digest);
}
} // namespace lockstep
