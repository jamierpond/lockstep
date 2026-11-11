#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lockstep
{
// A version as the numbers between its dots: 21.0.0.21000101.
struct DottedVersion
{
    std::vector<std::uint64_t> numbers;
};

// nullopt unless `text` is numbers separated by single dots.
std::optional<DottedVersion> parseDottedVersion(std::string_view text);

// Number by number, the shorter version counting as followed by zeros, so 21.0
// and 21.0.0.0 are equal.
std::strong_ordering compareVersions(const DottedVersion& first,
                                     const DottedVersion& second);

enum class VersionComparison
{
    atLeast,
    atMost,
    exactly
};

// One part of a range, such as >=21.0.0. It compares as many numbers of a
// version as it has itself, so =21.0.0 holds every 21.0.0.x.
struct VersionBound
{
    VersionComparison comparison = VersionComparison::exactly;
    DottedVersion version;
};

// Parts separated by spaces, all of which a version must meet:
// ">=21.0.0 <=21.0.4".
struct VersionRange
{
    std::vector<VersionBound> bounds;
    std::string text;

    // False for a version that is not numbers separated by dots.
    bool admits(std::string_view version) const;
};

// Throws std::invalid_argument saying what a range looks like.
VersionRange parseVersionRange(std::string_view text);
} // namespace lockstep
