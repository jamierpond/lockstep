#pragma once

#include <span>
#include <string_view>

// The test binary started as ssh: `ssh -T -o ... <host> <command>`. It runs
// <command> with this machine's sh, its stdin and stdout passed through, so a
// test reaches a library cache location over "ssh" without a host.
constexpr auto fakeSshFirstArgument = std::string_view {"-T"};

int runFakeSsh(std::span<char*> arguments);

// The test binary copied in as a compiler that answers `--version` with
// "InstalledDir: <the text of this file beside it>", as Clang does.
constexpr auto fakeCompilerVersionArgument = std::string_view {"--version"};
constexpr auto fakeCompilerInstalledDirectoryName = "installed-dir.txt";

bool isRunAsFakeCompiler(std::span<char*> arguments);
int runFakeCompiler();
