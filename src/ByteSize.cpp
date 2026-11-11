#include "ByteSize.h"

#include <eacp/Core/Utils/Format.h>
#include <eacp/Core/Utils/Strings.h>

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lockstep
{
namespace
{
struct ByteUnit
{
    std::string_view lowerCaseName;
    double bytes;
};

constexpr auto kibibyte = 1024.0;
constexpr auto kilobyte = 1000.0;

constexpr auto byteUnits =
    std::array<ByteUnit, 10> {{{"", 1.0},
                               {"b", 1.0},
                               {"kb", kilobyte},
                               {"mb", kilobyte* kilobyte},
                               {"gb", kilobyte * kilobyte * kilobyte},
                               {"tb", kilobyte * kilobyte * kilobyte* kilobyte},
                               {"kib", kibibyte},
                               {"mib", kibibyte* kibibyte},
                               {"gib", kibibyte * kibibyte * kibibyte},
                               {"tib", kibibyte * kibibyte * kibibyte* kibibyte}}};

struct DescribedUnit
{
    std::string_view name;
    double bytes;
};

// The binary units messages use, largest first.
constexpr auto describedUnits =
    std::array<DescribedUnit, 4> {{{"TiB", byteUnits[9].bytes},
                                   {"GiB", byteUnits[8].bytes},
                                   {"MiB", byteUnits[7].bytes},
                                   {"KiB", byteUnits[6].bytes}}};

constexpr auto sizeExample =
    std::string_view {"give a number and a unit, such as 5 GiB or 500 MB"};

std::invalid_argument createSizeError(std::string_view text)
{
    return std::invalid_argument {"'" + std::string {text}
                                  + "' is not a size: " + std::string {sizeExample}};
}

bool isNumberCharacter(char character)
{
    return (character >= '0' && character <= '9') || character == '.';
}

bool startsWithNumber(const std::string& text)
{
    return !text.empty() && isNumberCharacter(text.front());
}

double findUnitBytes(std::string_view text, const std::string& unit)
{
    auto lowerCaseUnit = eacp::Strings::toLower(unit);

    for (const auto& known: byteUnits)
    {
        if (known.lowerCaseName == lowerCaseUnit)
            return known.bytes;
    }

    throw createSizeError(text);
}
} // namespace

std::uint64_t parseByteSize(std::string_view text)
{
    auto trimmed = eacp::Strings::trim(std::string {text});
    auto numberEnd = trimmed.find_first_not_of("0123456789.");
    auto number = eacp::Strings::tryParse<double>(trimmed.substr(0, numberEnd));
    auto unit = std::string {};

    if (numberEnd != std::string::npos)
        unit = eacp::Strings::trim(trimmed.substr(numberEnd));

    if (!startsWithNumber(trimmed) || !number)
        throw createSizeError(text);

    auto bytes = std::floor(*number * findUnitBytes(text, unit));
    auto largest = static_cast<double>(std::numeric_limits<std::uint64_t>::max());

    if (bytes < 1.0 || bytes >= largest)
        throw std::invalid_argument {"'" + std::string {text}
                                     + "' is not a size lockstep can keep to: "
                                     + std::string {sizeExample}};

    return static_cast<std::uint64_t>(bytes);
}

std::string describeByteSize(std::uint64_t bytes)
{
    auto amount = static_cast<double>(bytes);

    for (const auto& unit: describedUnits)
    {
        if (amount >= unit.bytes)
            return eacp::Strings::format(
                "{:.1f} {}", amount / unit.bytes, unit.name);
    }

    return std::to_string(bytes) + " B";
}
} // namespace lockstep
