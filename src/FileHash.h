#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <filesystem>
#include <string>

namespace lockstep
{
// Lowercase hex SHA-256 of the file's bytes; throws std::runtime_error when the
// file cannot be opened, where eacp::computeFileSha256Hex returns "".
std::string computeFileSha256HexOrThrow(const std::filesystem::path& file);
std::string computeFileSha256HexOrThrow(const eacp::FilePath& file);
} // namespace lockstep
