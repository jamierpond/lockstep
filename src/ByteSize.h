#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lockstep
{
// A number of bytes with an optional unit: "5 GiB", "500MB", "1.5 GiB" or
// "4096". B, KB, MB, GB and TB count in thousands, KiB, MiB, GiB and TiB in
// 1024s, in any case. Throws std::invalid_argument for anything else, a size
// of zero, or one past what 64 bits hold.
std::uint64_t parseByteSize(std::string_view text);

// "33.5 MiB", for messages.
std::string describeByteSize(std::uint64_t bytes);
} // namespace lockstep
