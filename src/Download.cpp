#include "Download.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

#include <stdexcept>

namespace lockstep
{
namespace
{
constexpr auto downloadRetryCount = "3";
} // namespace

void downloadFile(const std::string& url, const eacp::FilePath& destination)
{
    auto partial = destination.sibling(destination.fileName() + ".part");
    eacp::Files::createDirectories(destination.parentDirectory());

    auto result = eacp::Processes::run("curl",
                                       {"--fail",
                                        "--silent",
                                        "--show-error",
                                        "--location",
                                        "--retry",
                                        downloadRetryCount,
                                        "--output",
                                        partial.str(),
                                        url});

    if (!result.launched)
        throw std::runtime_error("cannot run curl to download " + url
                                 + "; is it on PATH?");

    if (result.exitCode != 0)
    {
        eacp::Files::removeFile(partial);
        throw std::runtime_error("downloading " + url + " failed: "
                                 + eacp::Strings::trim(result.errorOutput));
    }

    if (!eacp::Files::moveFile(partial, destination))
        throw std::runtime_error("cannot move the download into "
                                 + destination.str());
}
} // namespace lockstep
