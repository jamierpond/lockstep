#pragma once

#include "Submodules.h"

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
    SubmoduleSelection submoduleSelection;
    std::string archiveSha256;
    bool isDownloadOnly = false;
    bool isExcludedFromAll = false;
    bool isSystem = false;
};

// Whose arguments these are: SOURCE_DIR is a local checkout to use for CPM,
// and only where to put the download for FetchContent_Declare.
enum class ArgumentDialect
{
    cpm,
    fetchContent
};

// The arguments of lockstep_add_package, CPMAddPackage (keyword or single-string
// "gh:owner/repo@1.2.3#tag" form, or URI <shorthand>) and FetchContent_Declare.
// Throws std::invalid_argument naming what is missing or malformed, and every
// option lockstep does not know or cannot honour.
PackageSpec parsePackageArguments(const std::vector<std::string>& arguments,
                                  ArgumentDialect dialect = ArgumentDialect::cpm);

// "Miro" for "https://github.com/eyalamirmusic/Miro.git" or
// ".../Miro-1.0.zip": the last path component without its extension.
std::string getPackageNameFromUrl(const std::string& url);
} // namespace lockstep
