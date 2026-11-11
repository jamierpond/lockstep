#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <filesystem>
#include <system_error>

namespace lockstep
{
// The path std::filesystem and Win32 can open at any length. On Windows it is
// absolute with a "\\?\" prefix and backslashes, which lifts MAX_PATH without
// the LongPathsEnabled registry key. eacp::FilePath cannot carry this form:
// wide() keeps '/' separators, which "\\?\" does not normalise.
std::filesystem::path getLongPathSafeStdPath(const eacp::FilePath& path);

void removeTreeAtAnyLength(const eacp::FilePath& path);

// Why the tree could not be deleted, or an empty error_code when it is gone.
std::error_code removeTreeAtAnyLengthAndGetError(const eacp::FilePath& path);

bool renameAtAnyLength(const eacp::FilePath& from, const eacp::FilePath& to);

// Why the rename failed, or an empty error_code when it succeeded.
std::error_code renameAtAnyLengthAndGetError(const eacp::FilePath& from,
                                             const eacp::FilePath& to);
} // namespace lockstep
