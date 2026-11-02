#include "Download.h"

#include <eacp/Core/Utils/Files.h>
#include <eacp/Network/HTTP/Http.h>

#include <stdexcept>

namespace lockstep
{
namespace
{
constexpr auto httpOkStatus = 200;
} // namespace

void downloadFile(const std::string& url, const eacp::FilePath& destination)
{
    auto partial = destination.sibling(destination.fileName() + ".part");
    eacp::Files::createDirectories(destination.parentDirectory());

    auto response =
        eacp::HTTP::downloadFile(eacp::HTTP::Request {url}, partial.str());

    if (!response.error.empty() || response.statusCode != httpOkStatus)
    {
        eacp::Files::removeFile(partial);
        throw std::runtime_error("downloading " + url + " failed with HTTP status "
                                 + std::to_string(response.statusCode) + ": "
                                 + response.error);
    }

    if (!eacp::Files::moveFile(partial, destination))
        throw std::runtime_error("cannot move the download into "
                                 + destination.str());
}
} // namespace lockstep
