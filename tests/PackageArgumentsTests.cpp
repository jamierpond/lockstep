#include <PackageArguments.h>

#include <NanoTest/NanoTest.h>

#include <stdexcept>

using namespace nano;
using lockstep::parsePackageArguments;

auto tGithubKeyword = test("Arguments/githubKeywordWithTag") = []
{
    auto spec = parsePackageArguments(
        {"NAME", "Miro", "GITHUB", "eyalamirmusic/Miro", "GIT_TAG", "main"});
    check(spec.name == "Miro");
    check(spec.kind == "git");
    check(spec.url == "https://github.com/eyalamirmusic/Miro.git");
    check(spec.ref == "main");
};

auto tGithubRepository = test("Arguments/githubRepositoryNamesFromRepo") = []
{
    auto spec = parsePackageArguments(
        {"GITHUB_REPOSITORY", "fmtlib/fmt", "GIT_TAG", "11.0.2"});
    check(spec.name == "fmt");
    check(spec.ref == "11.0.2");
};

auto tVersionBecomesTag = test("Arguments/versionBecomesVTag") = []
{
    auto spec = parsePackageArguments(
        {"NAME", "a", "GITLAB_REPOSITORY", "x/a", "VERSION", "1.2"});
    check(spec.url == "https://gitlab.com/x/a.git");
    check(spec.ref == "v1.2");
};

auto tShorthandVersion = test("Arguments/shorthandGithubAtVersion") = []
{
    auto spec = parsePackageArguments({"gh:cpm-cmake/testpack-adder@1.0.1"});
    check(spec.name == "testpack-adder");
    check(spec.url == "https://github.com/cpm-cmake/testpack-adder.git");
    check(spec.ref == "v1.0.1");
};

auto tShorthandTag = test("Arguments/shorthandTagWinsOverVersion") = []
{
    auto spec = parsePackageArguments({"gh:nlohmann/json@3.11.3#develop"});
    check(spec.ref == "develop");
};

auto tShorthandBitbucket = test("Arguments/shorthandBitbucketHashTag") = []
{
    auto spec = parsePackageArguments({"bb:team/repo#abc123"});
    check(spec.url == "https://bitbucket.org/team/repo.git");
    check(spec.ref == "abc123");
};

auto tShorthandGitUrl = test("Arguments/shorthandGitUrlAtVersion") = []
{
    auto spec = parsePackageArguments({"https://example.com/lib/thing.git@2.0"});
    check(spec.kind == "git");
    check(spec.url == "https://example.com/lib/thing.git");
    check(spec.name == "thing");
    check(spec.ref == "v2.0");
};

auto tShorthandSshUrl = test("Arguments/shorthandSshUrlKeepsItsAt") = []
{
    auto spec = parsePackageArguments({"git@github.com:owner/repo.git#v1"});
    check(spec.url == "git@github.com:owner/repo.git");
    check(spec.name == "repo");
    check(spec.ref == "v1");
};

auto tShorthandArchive = test("Arguments/shorthandArchiveIsUrl") = []
{
    auto spec = parsePackageArguments({"https://example.com/zlib-1.3.1.tar.gz"});
    check(spec.kind == "archive");
    check(spec.name == "zlib-1.3.1");
};

auto tUriKeyword = test("Arguments/uriKeywordWithOptions") = []
{
    auto spec = parsePackageArguments(
        {"URI", "gh:owner/lib@1.0", "OPTIONS", "LIB_TESTS OFF", "LIB_NAME a b"});
    check(spec.name == "lib");
    check(spec.ref == "v1.0");
    check(spec.options.size() == 2);
    check(spec.options[0].first == "LIB_TESTS");
    check(spec.options[0].second == "OFF");
    check(spec.options[1].second == "a b");
};

auto tFlagsTakeBooleans = test("Arguments/flagsTakeOptionalBooleans") = []
{
    auto spec = parsePackageArguments({"NAME",
                                       "a",
                                       "URL",
                                       "https://x/a.zip",
                                       "DOWNLOAD_ONLY",
                                       "YES",
                                       "EXCLUDE_FROM_ALL",
                                       "NO",
                                       "SYSTEM"});
    check(spec.isDownloadOnly);
    check(!spec.isExcludedFromAll);
    check(spec.isSystem);
};

auto tUnknownKeywordsIgnored = test("Arguments/unknownCpmKeywordsAreSkipped") = []
{
    auto spec = parsePackageArguments({"NAME",
                                       "a",
                                       "GITHUB_REPOSITORY",
                                       "o/a",
                                       "GIT_TAG",
                                       "v1",
                                       "GIT_SHALLOW",
                                       "TRUE",
                                       "SOURCE_SUBDIR",
                                       "cmake",
                                       "PATCHES",
                                       "a.patch",
                                       "b.patch"});
    check(spec.sourceSubdirectory == "cmake");
    check(spec.patches.size() == 2);
    check(spec.ref == "v1");
};

auto tFetchContentDetails = test("Arguments/fetchContentDeclareDetails") = []
{
    auto spec =
        parsePackageArguments({"NAME",
                               "adder",
                               "GIT_REPOSITORY",
                               "https://github.com/cpm-cmake/testpack-adder.git",
                               "GIT_TAG",
                               "v1.0.1",
                               "SOURCE_DIR",
                               "/tmp/somewhere"});
    check(spec.kind == "git");
    check(spec.name == "adder");
};

auto tSourceDirOnlyIsLocal = test("Arguments/sourceDirAloneIsLocal") = []
{
    auto spec = parsePackageArguments({"NAME", "mine", "SOURCE_DIR", "../mine"});
    check(spec.kind == "local");
    check(spec.localSourceDirectory == "../mine");
};

auto tPatchCommandNoticed = test("Arguments/patchCommandIsNoticed") = []
{
    auto spec = parsePackageArguments({"NAME",
                                       "a",
                                       "URL",
                                       "https://x/a.zip",
                                       "PATCH_COMMAND",
                                       "git",
                                       "apply",
                                       "x.patch"});
    check(spec.hasPatchCommand);
};

auto tMissingSourceThrows = test("Arguments/noSourceThrows") = []
{
    auto threw = false;

    try
    {
        parsePackageArguments({"NAME", "a", "GIT_TAG", "v1"});
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }

    check(threw);
};

auto tMissingTagThrows = test("Arguments/gitWithoutTagOrVersionThrows") = []
{
    auto threw = false;

    try
    {
        parsePackageArguments({"GITHUB_REPOSITORY", "o/a"});
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }

    check(threw);
};
