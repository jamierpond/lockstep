#pragma once

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

// True when every language the target compiles has its cache directory paths
// mapped away, in __FILE__ and in the debug information.
bool isCacheDirectoryMappedAway(const LibraryCacheInputs& inputs,
                                const std::string& target);
} // namespace lockstep
