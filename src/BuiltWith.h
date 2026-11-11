#pragma once

#include "VersionRange.h"

#include <Miro/Reflect.h>

#include <optional>
#include <string>
#include <vector>

namespace lockstep
{
// What a stored library or object file holds besides machine code: link-time
// optimisation leaves the compiler's intermediate code in it, for the linker to
// compile, and a linker reads only some compilers' intermediate code.
enum class IntermediateCode
{
    none,

    // Clang's bitcode: a linker reads what its own or an older Clang wrote.
    readableByNewerCompilers,

    // GCC's LTO bytecode and MSVC's /GL code: only the compiler version that
    // wrote it reads it.
    readableBySameCompilerVersionOnly
};

// The compiler of one language a target compiles, as it built a stored entry.
struct CompilerBuild
{
    std::string language;
    std::string version;

    // SHA-256 of the files the compiler really runs; see CompilerExecutable.h.
    std::string executableSha256;

    // CMAKE_<LANG>_COMPILER_LINKER_VERSION, for an executable's link.
    std::string linkerVersion;

    MIRO_REFLECT(language, version, executableSha256, linkerVersion)
};

// What built a stored entry, kept beside it rather than in its key: the
// versions that differ between machines whose builds can still share, when
// LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE or LOCKSTEP_LIBRARY_CACHE_SDK_RANGE says
// so. Several builds of one key sit side by side, one per record.
struct BuiltWith
{
    // One per language the target compiles, ordered by language.
    std::vector<CompilerBuild> compilers;
    std::string sdkVersion;
    std::string sdkBuildVersion;
    std::string cmakeVersion;

    // CMAKE_SYSTEM_VERSION when not cross-compiling; when cross-compiling it
    // is the target's version and stays in the key.
    std::string hostSystemVersion;
    IntermediateCode intermediateCode = IntermediateCode::none;

    // An executable's: the <key>/<build id> of every library it links.
    std::vector<std::string> linkedBuilds;

    MIRO_REFLECT(compilers,
                 sdkVersion,
                 sdkBuildVersion,
                 cmakeVersion,
                 hostSystemVersion,
                 intermediateCode,
                 linkedBuilds)
};

// The hex SHA-256 of the record's JSON: the name of a build under its key.
std::string computeBuildId(const BuiltWith& builtWith);

// The versions of other toolchains whose builds a lookup takes. Unset, only a
// build with the same record as this machine's is a hit.
struct ToolchainRanges
{
    std::optional<VersionRange> compiler;
    std::optional<VersionRange> sdk;

    bool isAnySet() const { return compiler || sdk; }
};

// Why this machine does not take a stored build of the key it looks up, or
// nullopt when it does. `thisMachine` is the record this machine's own build
// would have.
std::optional<std::string> findReasonToRefuseBuild(const BuiltWith& stored,
                                                   const BuiltWith& thisMachine,
                                                   const ToolchainRanges& ranges);

// The versions in `stored` that differ from `thisMachine`, for the configure's
// lines: "compiler CXX 21.0.0.21000101, SDK 26.5 (25F70)".
std::string describeDifferences(const BuiltWith& stored,
                                const BuiltWith& thisMachine);

// Greater when `first` was built by newer compilers, then a newer SDK, than
// `second`, so a lookup can take the newest build its ranges admit.
std::strong_ordering compareNewness(const BuiltWith& first, const BuiltWith& second);
} // namespace lockstep
