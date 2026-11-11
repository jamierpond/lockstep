#include "VersionRange.h"

#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace lockstep
{
namespace
{
struct ComparisonSpelling
{
    std::string_view prefix;
    VersionComparison comparison;
};

// Two-character prefixes first, so ">=" is not read as a stray ">".
constexpr auto comparisonSpellings =
    std::array<ComparisonSpelling, 3> {{{">=", VersionComparison::atLeast},
                                        {"<=", VersionComparison::atMost},
                                        {"=", VersionComparison::exactly}}};

constexpr auto rangeExample = std::string_view {
    ">=, <= or = followed by a dotted version, such as \">=21.0.0 <=21.0.4\""};

std::uint64_t getNumberOrZero(const DottedVersion& version, std::size_t index)
{
    if (index < version.numbers.size())
        return version.numbers[index];

    return 0;
}

// The first `count` numbers of the version, padded with zeros.
DottedVersion takeLeadingNumbers(const DottedVersion& version, std::size_t count)
{
    auto leading = DottedVersion {};

    for (auto index = std::size_t {0}; index < count; ++index)
        leading.numbers.push_back(getNumberOrZero(version, index));

    return leading;
}

bool meetsBound(const DottedVersion& version, const VersionBound& bound)
{
    auto leading = takeLeadingNumbers(version, bound.version.numbers.size());
    auto order = compareVersions(leading, bound.version);

    switch (bound.comparison)
    {
        case VersionComparison::atLeast:
            return order != std::strong_ordering::less;
        case VersionComparison::atMost:
            return order != std::strong_ordering::greater;
        case VersionComparison::exactly:
            return order == std::strong_ordering::equal;
    }

    return false;
}

// One space-separated part of a range, with the whole range for messages.
struct RangePart
{
    std::string_view range;
    std::string part;
};

std::invalid_argument createRangeError(const RangePart& part)
{
    return std::invalid_argument {"'" + std::string {part.range}
                                  + "' is not a version range: '" + part.part
                                  + "' is not " + std::string {rangeExample}};
}

VersionBound parseBound(const RangePart& part)
{
    for (const auto& spelling: comparisonSpellings)
    {
        if (!part.part.starts_with(spelling.prefix))
            continue;

        auto version = parseDottedVersion(
            std::string_view {part.part}.substr(spelling.prefix.size()));

        if (!version)
            throw createRangeError(part);

        return {spelling.comparison, *version};
    }

    throw createRangeError(part);
}

bool isDigit(char character)
{
    return character >= '0' && character <= '9';
}

std::optional<std::uint64_t> parseVersionNumber(const std::string& text)
{
    if (text.empty() || !std::ranges::all_of(text, isDigit))
        return std::nullopt;

    return eacp::Strings::tryParse<std::uint64_t>(text);
}
} // namespace

std::optional<DottedVersion> parseDottedVersion(std::string_view text)
{
    auto version = DottedVersion {};

    for (const auto& part: eacp::Strings::split(text, '.'))
    {
        auto number = parseVersionNumber(part);

        if (!number)
            return std::nullopt;

        version.numbers.push_back(*number);
    }

    if (version.numbers.empty())
        return std::nullopt;

    return version;
}

std::strong_ordering compareVersions(const DottedVersion& first,
                                     const DottedVersion& second)
{
    auto length = std::max(first.numbers.size(), second.numbers.size());

    for (auto index = std::size_t {0}; index < length; ++index)
    {
        auto order =
            getNumberOrZero(first, index) <=> getNumberOrZero(second, index);

        if (order != std::strong_ordering::equal)
            return order;
    }

    return std::strong_ordering::equal;
}

bool VersionRange::admits(std::string_view version) const
{
    auto parsed = parseDottedVersion(version);

    if (!parsed)
        return false;

    auto isMet = [&](const VersionBound& bound)
    { return meetsBound(*parsed, bound); };
    return std::ranges::all_of(bounds, isMet);
}

VersionRange parseVersionRange(std::string_view text)
{
    auto range = VersionRange {{}, std::string {text}};

    for (const auto& part: eacp::Strings::split(text, ' '))
    {
        if (!part.empty())
            range.bounds.push_back(parseBound({text, part}));
    }

    if (range.bounds.empty())
        throw std::invalid_argument {"an empty version range admits nothing; give "
                                     + std::string {rangeExample}};

    return range;
}
} // namespace lockstep
