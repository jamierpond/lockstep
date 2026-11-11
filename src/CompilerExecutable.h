#pragma once

#include "ConfigureDescription.h"

#include <eacp/Core/Utils/FilePath.h>

#include <string>
#include <vector>

namespace lockstep
{
// The files whose contents say which compiler really compiles, since a
// compiler's name and version do not: a patched compiler can report the same
// version. The configured path is often a launcher:
// - Clang and AppleClang: the binary in the directory `<compiler> --version`
//   names as InstalledDir, so on macOS the toolchain's clang, not the xcrun
//   shim in /usr/bin, and under nix the clang its wrapper runs;
// - GCC: the compiler proper `<compiler> -print-prog-name=cc1plus` (cc1 for
//   C, cc1obj and cc1objplus for Objective-C) names;
// - MSVC: cl.exe and the c1.dll, c1xx.dll and c2.dll beside it;
// - any other compiler, or one that does not answer: the configured file.
// A configured file that is a script, such as nix's wrapper, which adds flags
// of its own, comes first. Symlinks are followed. Empty when none is a file.
std::vector<eacp::FilePath>
    findCompilerExecutableFiles(const ConfiguredCompiler& compiler);

// Every compiler a directory with targets names, hashed: the SHA-256 of the
// one file's contents, or of the files' SHA-256s one per line when there are
// several. Empty when none could be read, which then matches only another
// empty one, as a compiler's name and version alone did. A file's SHA-256 is
// remembered in <directory>/compiler-executables.json by its path, size and
// modification time, so a configure rehashes only a compiler that changed.
CompilerExecutableHashes
    hashCompilerExecutables(const ConfigureDescription& description,
                            const eacp::FilePath& rememberedHashesDirectory);

// The compiler of `language` the directory's fields name.
ConfiguredCompiler findConfiguredCompiler(const RecordFields& directoryFields,
                                          const std::string& language);
} // namespace lockstep
