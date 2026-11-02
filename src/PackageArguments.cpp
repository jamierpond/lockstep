#include "PackageArguments.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

namespace lockstep
{
namespace
{
constexpr auto valueKeywords =
    std::array<std::string_view, 17> {"NAME",
                                      "VERSION",
                                      "GIT_TAG",
                                      "GITHUB",
                                      "GITHUB_REPOSITORY",
                                      "GITLAB_REPOSITORY",
                                      "BITBUCKET_REPOSITORY",
                                      "GIT_REPOSITORY",
                                      "SOURCE_DIR",
                                      "SOURCE_SUBDIR",
                                      "URI",
                                      "FORCE",
                                      "GIT_SHALLOW",
                                      "CUSTOM_CACHE_KEY",
                                      "NO_CACHE",
                                      "FIND_PACKAGE_ARGUMENTS",
                                      "URL_HASH"};

constexpr auto flagKeywords =
    std::array<std::string_view, 3> {"DOWNLOAD_ONLY", "EXCLUDE_FROM_ALL", "SYSTEM"};

constexpr auto listKeywords = std::array<std::string_view, 6> {"URL",
                                                               "OPTIONS",
                                                               "PATCHES",
                                                               "PATCH_COMMAND",
                                                               "DOWNLOAD_COMMAND",
                                                               "FIND_PACKAGE_ARGS"};

constexpr auto archiveExtensions = std::array<std::string_view, 7> {
    ".zip", ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tar"};

constexpr auto falseWords =
    std::array<std::string_view, 6> {"NO", "OFF", "FALSE", "0", "N", "IGNORE"};

constexpr auto booleanWords = std::array<std::string_view, 12> {
    "YES", "ON", "TRUE", "1", "Y", "NO", "OFF", "FALSE", "0", "N", "IGNORE", ""};

template <std::size_t Size>
bool isOneOf(const std::array<std::string_view, Size>& words, std::string_view text)
{
    return std::find(words.begin(), words.end(), text) != words.end();
}

bool isKeyword(std::string_view text)
{
    return isOneOf(valueKeywords, text) || isOneOf(flagKeywords, text)
           || isOneOf(listKeywords, text);
}

bool hasSuffix(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size()
           && text.substr(text.size() - suffix.size()) == suffix;
}

bool isArchiveUrl(std::string_view url)
{
    return std::any_of(archiveExtensions.begin(),
                       archiveExtensions.end(),
                       [&](auto extension) { return hasSuffix(url, extension); });
}

struct KeywordValues
{
    std::vector<std::pair<std::string, std::string>> values;
    std::vector<std::pair<std::string, std::vector<std::string>>> lists;
    std::vector<std::pair<std::string, bool>> flags;

    std::optional<std::string> findValue(std::string_view keyword) const
    {
        for (const auto& [key, value]: values)
        {
            if (key == keyword)
                return value;
        }

        return std::nullopt;
    }

    std::vector<std::string> findList(std::string_view keyword) const
    {
        for (const auto& [key, list]: lists)
        {
            if (key == keyword)
                return list;
        }

        return {};
    }

    bool hasList(std::string_view keyword) const
    {
        return std::any_of(lists.begin(),
                           lists.end(),
                           [&](const auto& entry)
                           { return entry.first == keyword; });
    }

    bool isFlagSet(std::string_view keyword) const
    {
        for (const auto& [key, isSet]: flags)
        {
            if (key == keyword)
                return isSet;
        }

        return false;
    }
};

KeywordValues collectKeywordValues(const std::vector<std::string>& arguments)
{
    auto collected = KeywordValues {};
    auto index = std::size_t {0};

    while (index < arguments.size())
    {
        const auto& keyword = arguments[index++];

        if (isOneOf(flagKeywords, keyword))
        {
            auto isSet = true;

            if (index < arguments.size() && isOneOf(booleanWords, arguments[index]))
                isSet = !isOneOf(falseWords, arguments[index++]);

            collected.flags.emplace_back(keyword, isSet);
        }
        else if (isOneOf(valueKeywords, keyword))
        {
            if (index >= arguments.size())
                throw std::invalid_argument(keyword + " needs a value");

            collected.values.emplace_back(keyword, arguments[index++]);
        }
        else if (isOneOf(listKeywords, keyword))
        {
            auto list = std::vector<std::string> {};

            while (index < arguments.size() && !isKeyword(arguments[index]))
                list.push_back(arguments[index++]);

            collected.lists.emplace_back(keyword, list);
        }
    }

    return collected;
}

std::vector<std::string> expandShorthand(std::string text)
{
    auto expanded = std::vector<std::string> {};
    auto tag = std::string {};
    auto version = std::string {};

    if (auto hash = text.rfind('#'); hash != std::string::npos)
    {
        tag = text.substr(hash + 1);
        text.resize(hash);
    }

    if (auto at = text.rfind('@'); at != std::string::npos && at > 0
                                   && text.find('/', at) == std::string::npos
                                   && text[at - 1] != ':')
    {
        version = text.substr(at + 1);
        text.resize(at);
    }

    if (text.starts_with("gh:"))
        expanded = {"GITHUB_REPOSITORY", text.substr(3)};
    else if (text.starts_with("gl:"))
        expanded = {"GITLAB_REPOSITORY", text.substr(3)};
    else if (text.starts_with("bb:"))
        expanded = {"BITBUCKET_REPOSITORY", text.substr(3)};
    else if (isArchiveUrl(text))
        expanded = {"URL", text};
    else
        expanded = {"GIT_REPOSITORY", text};

    if (!version.empty())
        expanded.insert(expanded.end(), {"VERSION", version});

    if (!tag.empty())
        expanded.insert(expanded.end(), {"GIT_TAG", tag});

    return expanded;
}

std::vector<std::string> expandArguments(const std::vector<std::string>& arguments)
{
    if (arguments.size() == 1 && !isKeyword(arguments[0]))
        return expandShorthand(arguments[0]);

    auto expanded = std::vector<std::string> {};

    for (auto index = std::size_t {0}; index < arguments.size(); ++index)
    {
        if (arguments[index] == "URI" && index + 1 < arguments.size())
        {
            auto uriArguments = expandShorthand(arguments[++index]);
            expanded.insert(
                expanded.end(), uriArguments.begin(), uriArguments.end());
            continue;
        }

        expanded.push_back(arguments[index]);
    }

    return expanded;
}

std::pair<std::string, std::string> splitOption(const std::string& option)
{
    auto space = option.find(' ');

    if (space == std::string::npos)
        throw std::invalid_argument("OPTIONS entry '" + option
                                    + "' is not 'NAME VALUE'");

    auto value = option.substr(space + 1);
    auto valueStart = value.find_first_not_of(' ');

    if (valueStart == std::string::npos)
        value.clear();
    else
        value = value.substr(valueStart);

    return {option.substr(0, space), value};
}

void setSourceFromKeywords(PackageSpec& spec, const KeywordValues& keywords)
{
    auto github = keywords.findValue("GITHUB_REPOSITORY");

    if (!github)
        github = keywords.findValue("GITHUB");

    if (github)
    {
        spec.kind = "git";
        spec.url = "https://github.com/" + *github + ".git";
    }
    else if (auto gitlab = keywords.findValue("GITLAB_REPOSITORY"))
    {
        spec.kind = "git";
        spec.url = "https://gitlab.com/" + *gitlab + ".git";
    }
    else if (auto bitbucket = keywords.findValue("BITBUCKET_REPOSITORY"))
    {
        spec.kind = "git";
        spec.url = "https://bitbucket.org/" + *bitbucket + ".git";
    }
    else if (auto repository = keywords.findValue("GIT_REPOSITORY"))
    {
        spec.kind = "git";
        spec.url = *repository;
    }
    else if (auto urls = keywords.findList("URL"); !urls.empty())
    {
        spec.kind = "archive";
        spec.url = urls.front();
    }
    else if (auto local = keywords.findValue("SOURCE_DIR"))
    {
        spec.kind = "local";
        spec.localSourceDirectory = *local;
    }
    else
    {
        throw std::invalid_argument("a package needs GITHUB, GITHUB_REPOSITORY, "
                                    "GITLAB_REPOSITORY, BITBUCKET_REPOSITORY, "
                                    "GIT_REPOSITORY, URL or SOURCE_DIR");
    }
}
} // namespace

std::string getPackageNameFromUrl(const std::string& url)
{
    auto path = url.substr(0, url.find_first_of("?#"));

    while (!path.empty() && path.back() == '/')
        path.pop_back();

    auto name = path.substr(path.find_last_of("/:") + 1);

    for (auto extension: archiveExtensions)
    {
        if (hasSuffix(name, extension))
            return name.substr(0, name.size() - extension.size());
    }

    if (hasSuffix(name, ".git"))
        return name.substr(0, name.size() - 4);

    return name;
}

PackageSpec parsePackageArguments(const std::vector<std::string>& arguments)
{
    auto keywords = collectKeywordValues(expandArguments(arguments));
    auto spec = PackageSpec {};

    setSourceFromKeywords(spec, keywords);

    spec.name = keywords.findValue("NAME").value_or("");

    if (spec.name.empty() && spec.kind != "local")
        spec.name = getPackageNameFromUrl(spec.url);

    if (spec.name.empty())
        throw std::invalid_argument("a SOURCE_DIR package needs a NAME");

    spec.ref = keywords.findValue("GIT_TAG").value_or("");

    if (spec.kind == "git" && spec.ref.empty())
    {
        auto version = keywords.findValue("VERSION");

        if (!version)
            throw std::invalid_argument(spec.name + " needs a GIT_TAG or a VERSION");

        spec.ref = "v" + *version;
    }

    for (const auto& option: keywords.findList("OPTIONS"))
        spec.options.push_back(splitOption(option));

    spec.patches = keywords.findList("PATCHES");
    spec.sourceSubdirectory = keywords.findValue("SOURCE_SUBDIR").value_or("");
    spec.isDownloadOnly = keywords.isFlagSet("DOWNLOAD_ONLY");
    spec.isExcludedFromAll = keywords.isFlagSet("EXCLUDE_FROM_ALL");
    spec.isSystem = keywords.isFlagSet("SYSTEM");
    spec.hasPatchCommand = keywords.hasList("PATCH_COMMAND");
    return spec;
}
} // namespace lockstep
