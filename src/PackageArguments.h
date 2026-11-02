#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lockstep
{
struct PackageSpec
{
    std::string name;
    std::string kind;
    std::string url;
    std::string ref;
    std::string localSourceDirectory;
    std::string sourceSubdirectory;
    std::vector<std::pair<std::string, std::string>> options;
    std::vector<std::string> patches;
    bool isDownloadOnly = false;
    bool isExcludedFromAll = false;
    bool isSystem = false;
    bool hasPatchCommand = false;
};

// The arguments of lockstep_add_package, CPMAddPackage (keyword or single-string
// "gh:owner/repo@1.2.3#tag" form, or URI <shorthand>) and FetchContent_Declare.
// Throws std::invalid_argument naming what is missing or malformed.
PackageSpec parsePackageArguments(const std::vector<std::string>& arguments);

// "Miro" for "https://github.com/eyalamirmusic/Miro.git" or
// ".../Miro-1.0.zip": the last path component without its extension.
std::string getPackageNameFromUrl(const std::string& url);
} // namespace lockstep
