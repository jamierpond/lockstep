#pragma once

#include "BuiltWith.h"
#include "KeyText.h"

#include <string>

namespace lockstep
{
// The record of this machine's build of the target, from the fields `filter`
// keeps in its key: CMAKE_<LANG>_COMPILER_VERSION and the hash of that
// compiler's executable, CMAKE_<LANG>_COMPILER_LINKER_VERSION,
// APPLE_SDK_VERSION, APPLE_SDK_BUILD_VERSION, CMAKE_VERSION and, when not
// cross-compiling, CMAKE_SYSTEM_VERSION. `filter` then leaves those fields out,
// so the key holds everything else.
BuiltWith moveBuiltWithFieldsOutOfKey(const LibraryCacheInputs& inputs,
                                      const std::string& target,
                                      KeyFieldFilter& filter);

// The key of `inputs`, with the record of this machine's build and its id.
LibraryKey createLibraryKey(const std::string& inputs, const BuiltWith& builtWith);
} // namespace lockstep
