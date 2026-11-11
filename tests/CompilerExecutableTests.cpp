#include "FakeTools.h"
#include "TestDirectory.h"

#include <CompilerExecutable.h>
#include <Sha256.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Utils/StdPath.h>

#include <chrono>
#include <filesystem>
#include <iostream>

using namespace nano;

namespace CompilerExecutableTests
{
constexpr auto testExecutablePath = LOCKSTEP_TESTS_EXECUTABLE;
constexpr auto executablePermissions = std::filesystem::perms::owner_all
                                       | std::filesystem::perms::group_read
                                       | std::filesystem::perms::others_read;

void writeScript(const eacp::FilePath& path, const std::string& text)
{
    writeTestFile(path, text);
    std::filesystem::permissions(eacp::toStdPath(path), executablePermissions);
}

eacp::FilePath findCanonical(const eacp::FilePath& path)
{
    return eacp::FilePath {
        std::filesystem::canonical(eacp::toStdPath(path)).generic_string()};
}

lockstep::ConfigureDescription
    describeCompiler(const lockstep::ConfiguredCompiler& compiler)
{
    return lockstep::parseConfigureDescription(
        "global\t-\tENABLED_LANGUAGES\t" + compiler.language
        + "\n"
          "directory\t/src\tCMAKE_"
        + compiler.language + "_COMPILER\t" + compiler.path
        + "\n"
          "directory\t/src\tCMAKE_"
        + compiler.language + "_COMPILER_ID\t" + compiler.compilerId + "\n");
}

std::string hashCompiler(const lockstep::ConfiguredCompiler& compiler,
                         const eacp::FilePath& rememberedHashesDirectory)
{
    return lockstep::hashCompilerExecutables(describeCompiler(compiler),
                                             rememberedHashesDirectory)
        .at(compiler);
}
} // namespace CompilerExecutableTests

using namespace CompilerExecutableTests;

#if !defined(_WIN32)
auto tClangBehindWrapper = test(
    "CompilerExecutable/clangIsTheBinaryItsVersionNamesAfterTheWrapperScript") = []
{
    auto directory = createEmptyTestDirectory("compiler-clang-wrapper");
    auto real = directory / "toolchain" / "bin" / "clang-21";
    writeTestFile(real, "the real compiler");
    std::filesystem::create_symlink(
        "clang-21", eacp::toStdPath(directory / "toolchain" / "bin" / "clang++"));
    auto wrapper = directory / "wrapper" / "clang++";
    writeScript(wrapper,
                "#!/bin/sh\necho 'clang version 21.1.8'\necho 'InstalledDir: "
                    + (directory / "toolchain" / "bin").str() + "'\n");

    auto files =
        lockstep::findCompilerExecutableFiles({"CXX", "Clang", wrapper.str()});

    check(files.size() == 2);
    check(files.size() == 2 && files[0] == findCanonical(wrapper));
    check(files.size() == 2 && files[1] == findCanonical(real));
};

auto tShimLeftOut =
    test("CompilerExecutable/aLauncherThatIsNotAScriptIsLeftOut") = []
{
    auto directory = createEmptyTestDirectory("compiler-shim");
    auto real = directory / "toolchain" / "c++";
    writeTestFile(real, "the toolchain's clang");
    auto shim = directory / "usr-bin" / "c++";
    eacp::Files::createDirectories(shim.parentDirectory());
    check(eacp::Files::copyFile(eacp::FilePath {testExecutablePath}, shim));
    std::filesystem::permissions(eacp::toStdPath(shim), executablePermissions);
    writeTestFile(shim.sibling(fakeCompilerInstalledDirectoryName),
                  (directory / "toolchain").str());

    auto files =
        lockstep::findCompilerExecutableFiles({"CXX", "AppleClang", shim.str()});

    check(files.size() == 1 && files.front() == findCanonical(real));
};

auto tGccProper = test("CompilerExecutable/gccIsTheCompilerProperItNames") = []
{
    auto directory = createEmptyTestDirectory("compiler-gcc");
    auto cc1plus = directory / "libexec" / "cc1plus";
    auto cc1 = directory / "libexec" / "cc1";
    writeTestFile(cc1plus, "C++ compiler proper");
    writeTestFile(cc1, "C compiler proper");
    auto driver = directory / "bin" / "gcc";
    writeScript(driver,
                "#!/bin/sh\ncase \"$1\" in\n"
                "-print-prog-name=cc1plus) echo '"
                    + cc1plus.str()
                    + "';;\n"
                      "-print-prog-name=cc1) echo '"
                    + cc1.str()
                    + "';;\n"
                      "esac\n");

    auto cxx = lockstep::findCompilerExecutableFiles({"CXX", "GNU", driver.str()});
    auto c = lockstep::findCompilerExecutableFiles({"C", "GNU", driver.str()});

    check(cxx.size() == 2 && cxx[1] == findCanonical(cc1plus));
    check(c.size() == 2 && c[1] == findCanonical(cc1));
};

auto tSilentCompiler =
    test("CompilerExecutable/aCompilerThatDoesNotAnswerIsItsOwnFile") = []
{
    auto directory = createEmptyTestDirectory("compiler-silent");
    auto compiler = directory / "bin" / "clang";
    writeScript(compiler, "#!/bin/sh\nexit 1\n");

    auto files =
        lockstep::findCompilerExecutableFiles({"C", "Clang", compiler.str()});

    check(files.size() == 1 && files.front() == findCanonical(compiler));
};
#endif

auto tMsvcFiles = test("CompilerExecutable/msvcIsClAndTheCompilerDllsBesideIt") = []
{
    auto directory = createEmptyTestDirectory("compiler-msvc");
    auto cl = directory / "bin" / "cl.exe";
    writeTestFile(cl, "driver");
    writeTestFile(directory / "bin" / "c1xx.dll", "front end");
    writeTestFile(directory / "bin" / "c2.dll", "back end");

    auto files = lockstep::findCompilerExecutableFiles({"CXX", "MSVC", cl.str()});

    check(files.size() == 3);
    check(files.size() == 3 && files[1].fileName() == "c1xx.dll"
          && files[2].fileName() == "c2.dll");
};

auto tMissingCompiler = test("CompilerExecutable/aMissingCompilerHashesAsEmpty") = []
{
    auto directory = createEmptyTestDirectory("compiler-missing");
    auto compiler = lockstep::ConfiguredCompiler {"CXX", "GNU", "/no/such/compiler"};
    check(hashCompiler(compiler, directory).empty());
};

auto tRemembersHashes =
    test("CompilerExecutable/remembersAHashUntilTheFileChangesSizeOrTime") = []
{
    auto directory = createEmptyTestDirectory("compiler-remembered");
    auto cl = directory / "bin" / "cl.exe";
    writeTestFile(cl, "first");
    auto compiler = lockstep::ConfiguredCompiler {"CXX", "Other", cl.str()};
    auto stdPath = eacp::toStdPath(cl);
    auto stamp = std::filesystem::last_write_time(stdPath);

    auto first = hashCompiler(compiler, directory);
    check(first == lockstep::computeSha256Hex("first"));
    check(eacp::Files::isFile(directory / "compiler-executables.json"));

    writeTestFile(cl, "other");
    std::filesystem::last_write_time(stdPath, stamp);
    check(hashCompiler(compiler, directory) == first);

    std::filesystem::last_write_time(stdPath, stamp + std::chrono::seconds {2});
    check(hashCompiler(compiler, directory) == lockstep::computeSha256Hex("other"));
};

bool isRunAsFakeCompiler(std::span<char*> arguments)
{
    auto marker =
        eacp::Files::executablePath().sibling(fakeCompilerInstalledDirectoryName);
    return arguments.size() == 2 && arguments[1] == fakeCompilerVersionArgument
           && eacp::Files::isFile(marker);
}

int runFakeCompiler()
{
    auto marker =
        eacp::Files::executablePath().sibling(fakeCompilerInstalledDirectoryName);
    std::cout << "Apple clang version 21.0.0\nInstalledDir: "
              << eacp::Files::readFile(marker) << "\n";
    return 0;
}
