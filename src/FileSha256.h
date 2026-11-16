#pragma once

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Sha256.h>

#include <filesystem>
#include <string>

// eacp's computeFileSha256 maps the file and returns empty when it can't be
// opened; these stream it, throw instead, and take Windows paths past MAX_PATH.
namespace lockstep
{
eacp::Sha256Digest computeFileSha256(const eacp::FilePath& file);
eacp::Sha256Digest computeFileSha256(const std::filesystem::path& file);
std::string computeFileSha256Hex(const eacp::FilePath& file);
std::string computeFileSha256Hex(const std::filesystem::path& file);
} // namespace lockstep
