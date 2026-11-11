#include "BuiltWith.h"
#include "Sha256.h"

#include <eacp/Core/Utils/Strings.h>

#include <Miro/Json.h>

#include <algorithm>

namespace lockstep
{
namespace
{
constexpr auto shortHashLength = std::size_t {12};
constexpr auto compilerRangeVariable =
    std::string_view {"LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE"};
constexpr auto sdkRangeVariable =
    std::string_view {"LOCKSTEP_LIBRARY_CACHE_SDK_RANGE"};

std::optional<CompilerBuild> findCompiler(const BuiltWith& builtWith,
                                          const std::string& language)
{
    auto isLanguage = [&](const CompilerBuild& compiler)
    { return compiler.language == language; };
    auto found = std::ranges::find_if(builtWith.compilers, isLanguage);

    if (found == builtWith.compilers.end())
        return std::nullopt;

    return *found;
}

std::string describeSdk(const BuiltWith& builtWith)
{
    return "SDK " + builtWith.sdkVersion + " (" + builtWith.sdkBuildVersion + ")";
}

void addCompilerDifferences(eacp::Vector<std::string>& differences,
                            const CompilerBuild& stored,
                            const CompilerBuild& thisMachine)
{
    auto name = stored.language + " compiler";

    if (stored.version != thisMachine.version)
        differences.add(name + " " + stored.version);
    else if (stored.executableSha256 != thisMachine.executableSha256)
        differences.add(name + " executable "
                        + stored.executableSha256.substr(0, shortHashLength));

    if (stored.linkerVersion != thisMachine.linkerVersion)
        differences.add(stored.language + " linker " + stored.linkerVersion);
}

void addEveryCompilerDifference(eacp::Vector<std::string>& differences,
                                const BuiltWith& stored,
                                const BuiltWith& thisMachine)
{
    for (const auto& compiler: stored.compilers)
    {
        if (auto own = findCompiler(thisMachine, compiler.language))
            addCompilerDifferences(differences, compiler, *own);
        else
            differences.add(compiler.language + " compiler " + compiler.version);
    }
}

bool isSameCompiler(const CompilerBuild& stored, const CompilerBuild& thisMachine)
{
    return stored.version == thisMachine.version
           && stored.executableSha256 == thisMachine.executableSha256
           && stored.linkerVersion == thisMachine.linkerVersion;
}

std::optional<std::string>
    findReasonInIntermediateCode(const CompilerBuild& stored,
                                 const CompilerBuild& thisMachine,
                                 IntermediateCode intermediateCode)
{
    if (intermediateCode == IntermediateCode::none
        || stored.version == thisMachine.version)
        return std::nullopt;

    auto storedVersion = parseDottedVersion(stored.version);
    auto ownVersion = parseDottedVersion(thisMachine.version);
    auto prefix = "it holds link-time optimisation code from " + stored.language
                  + " compiler " + stored.version + ", ";

    if (intermediateCode == IntermediateCode::readableBySameCompilerVersionOnly)
        return prefix
               + "which only that compiler version reads, and this machine's is "
               + thisMachine.version;

    if (!storedVersion || !ownVersion)
        return prefix + "which cannot be compared with this machine's "
               + thisMachine.version;

    if (compareVersions(*storedVersion, *ownVersion)
        == std::strong_ordering::greater)
        return prefix + "newer than this machine's " + thisMachine.version
               + ", whose linker cannot read it";

    return std::nullopt;
}

std::optional<std::string> findReasonInCompiler(const CompilerBuild& stored,
                                                const CompilerBuild& thisMachine,
                                                const ToolchainRanges& ranges,
                                                IntermediateCode intermediateCode)
{
    if (!ranges.compiler)
    {
        if (isSameCompiler(stored, thisMachine))
            return std::nullopt;

        auto differences = eacp::Vector<std::string> {};
        addCompilerDifferences(differences, stored, thisMachine);
        return "it was built by " + eacp::Strings::join(differences, ", ") + ", and "
               + std::string {compilerRangeVariable} + " is not set";
    }

    if (!ranges.compiler->admits(stored.version))
        return "its " + stored.language + " compiler " + stored.version
               + " is outside the compiler range " + ranges.compiler->text;

    return findReasonInIntermediateCode(stored, thisMachine, intermediateCode);
}

std::optional<std::string> findReasonInCompilers(const BuiltWith& stored,
                                                 const BuiltWith& thisMachine,
                                                 const ToolchainRanges& ranges)
{
    if (stored.compilers.size() != thisMachine.compilers.size())
        return std::string {"it was built for other languages"};

    for (const auto& own: thisMachine.compilers)
    {
        auto compiler = findCompiler(stored, own.language);

        if (!compiler)
            return "it was built without a " + own.language + " compiler";

        if (auto reason = findReasonInCompiler(
                *compiler, own, ranges, stored.intermediateCode))
            return reason;
    }

    return std::nullopt;
}

std::optional<std::string> findReasonInSdk(const BuiltWith& stored,
                                           const BuiltWith& thisMachine,
                                           const ToolchainRanges& ranges)
{
    if (stored.sdkVersion == thisMachine.sdkVersion
        && stored.sdkBuildVersion == thisMachine.sdkBuildVersion)
        return std::nullopt;

    if (!ranges.sdk)
        return "it was built with " + describeSdk(stored) + ", and "
               + std::string {sdkRangeVariable} + " is not set";

    if (!ranges.sdk->admits(stored.sdkVersion))
        return "its " + describeSdk(stored) + " is outside the SDK range "
               + ranges.sdk->text;

    return std::nullopt;
}

std::strong_ordering compareVersionTexts(const std::string& first,
                                         const std::string& second)
{
    auto firstVersion = parseDottedVersion(first);
    auto secondVersion = parseDottedVersion(second);

    if (!firstVersion || !secondVersion)
        return first <=> second;

    return compareVersions(*firstVersion, *secondVersion);
}
} // namespace

std::string computeBuildId(const BuiltWith& builtWith)
{
    return computeSha256Hex(Miro::toJSONString(builtWith));
}

std::optional<std::string> findReasonToRefuseBuild(const BuiltWith& stored,
                                                   const BuiltWith& thisMachine,
                                                   const ToolchainRanges& ranges)
{
    if (computeBuildId(stored) == computeBuildId(thisMachine))
        return std::nullopt;

    if (!ranges.isAnySet())
        return "it was built with " + describeDifferences(stored, thisMachine)
               + ", and neither " + std::string {compilerRangeVariable} + " nor "
               + std::string {sdkRangeVariable} + " is set";

    if (stored.linkedBuilds != thisMachine.linkedBuilds)
        return std::string {
            "it links other builds of its libraries than this build directory links"};

    if (auto reason = findReasonInCompilers(stored, thisMachine, ranges))
        return reason;

    return findReasonInSdk(stored, thisMachine, ranges);
}

std::string describeDifferences(const BuiltWith& stored,
                                const BuiltWith& thisMachine)
{
    auto differences = eacp::Vector<std::string> {};
    addEveryCompilerDifference(differences, stored, thisMachine);

    if (stored.sdkVersion != thisMachine.sdkVersion
        || stored.sdkBuildVersion != thisMachine.sdkBuildVersion)
        differences.add(describeSdk(stored));

    if (stored.cmakeVersion != thisMachine.cmakeVersion)
        differences.add("CMake " + stored.cmakeVersion);

    if (stored.hostSystemVersion != thisMachine.hostSystemVersion)
        differences.add("system " + stored.hostSystemVersion);

    if (stored.linkedBuilds != thisMachine.linkedBuilds)
        differences.add("other builds of its libraries");

    if (differences.empty())
        return "the same toolchain";

    return eacp::Strings::join(differences, ", ");
}

std::strong_ordering compareNewness(const BuiltWith& first, const BuiltWith& second)
{
    auto count = std::min(first.compilers.size(), second.compilers.size());

    for (auto index = std::size_t {0}; index < count; ++index)
    {
        auto order = compareVersionTexts(first.compilers[index].version,
                                         second.compilers[index].version);

        if (order != std::strong_ordering::equal)
            return order;
    }

    return compareVersionTexts(first.sdkVersion, second.sdkVersion);
}
} // namespace lockstep
