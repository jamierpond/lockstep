#pragma once

#include "BuiltWith.h"
#include "ConfigureDescription.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lockstep
{
// True when `flag` stands as a whole word in `text`, not as part of a longer
// flag or path.
bool containsFlag(std::string_view text, std::string_view flag);

bool compilesForMsvcAbi(const RecordFields& directoryFields,
                        const std::vector<std::string>& compiledLanguages);

// Why MSVC would write the target's debug information to a separate .pdb, if
// it would.
std::optional<std::string>
    findReasonInDebugInformation(const ConfigureDescription& description,
                                 const std::string& target);

std::optional<std::string>
    findReasonInSplitDebugInformation(const ConfigureDescription& description,
                                      const std::string& target);

// What the target's objects hold besides machine code: with link-time
// optimisation (-flto in any form, INTERPROCEDURAL_OPTIMIZATION or MSVC's /GL)
// the compiler's intermediate code, which GCC and MSVC read back only at the
// version that wrote it, and Clang at that version or newer.
IntermediateCode findIntermediateCode(const ConfigureDescription& description,
                                      const std::string& target);

// True when every language the target compiles has its cache directory paths
// mapped away, in __FILE__ and in the debug information.
bool isCacheDirectoryMappedAway(const LibraryCacheInputs& inputs,
                                const std::string& target);
} // namespace lockstep
