#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <string>

namespace lockstep
{
// Downloads `url` to `destination`, writing a .part sibling first so an
// interrupted download never leaves a whole-looking file.
void downloadFile(const std::string& url, const eacp::FilePath& destination);
} // namespace lockstep
