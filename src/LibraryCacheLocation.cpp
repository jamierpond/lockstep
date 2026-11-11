#include "LibraryCacheLocation.h"
#include "Sha256.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace lockstep
{
namespace
{
constexpr auto sshConnectTimeoutSeconds = 5;
constexpr auto sshKeepAliveIntervalSeconds = 5;
constexpr auto sshKeepAliveMissesBeforeGivingUp = 3;
constexpr auto fetchTimeLimit = std::chrono::seconds {60};
constexpr auto sendTimeLimit = std::chrono::seconds {600};
constexpr auto processPollInterval = std::chrono::milliseconds {20};
constexpr auto driveLetterLength = std::size_t {1};
constexpr auto homeDirectoryPrefix = std::string_view {"~/"};
constexpr auto lacksMarker = std::string_view {"lockstep-location-lacks "};
constexpr auto fetchedArchiveName = "fetched.tar";

// Run by the host's sh with the format directory as $1 and, as $2, 1 when every
// build of a missing key is wanted where the one asked for is not there. It
// reads "missing <key> <build id>" and "copied <key> <build id>" lines, writes a
// tar of every missing build it holds to stdout and names every copied build
// it lacks on stderr.
constexpr auto fetchScript = std::string_view {
    "if cd \"$1\" 2>/dev/null; then held=1; else held=0; fi; others=$2; found=; "
    "while read kind key build; do "
    "if [ $held = 1 ] && [ -f \"$key/$build/manifest.json\" ]; then "
    "if [ \"$kind\" = missing ]; then found=\"$found $key/$build\"; fi; "
    "elif [ \"$kind\" = copied ]; then "
    "echo \"lockstep-location-lacks $key/$build\" >&2; "
    "elif [ $held = 1 ] && [ \"$others\" = 1 ] && [ -d \"$key\" ]; then "
    "for entry in \"$key\"/*; do "
    "if [ -f \"$entry/manifest.json\" ]; then found=\"$found $entry\"; fi; "
    "done; "
    "fi; "
    "done; "
    "if [ -n \"$found\" ]; then COPYFILE_DISABLE=1 exec tar -cf - $found; fi"};

// Run by the host's sh with the format directory as $1 and each build's
// <key>/<build id> after it, reading a tar of them. Each is unpacked beside the
// entries under a random name and renamed into place, so a half-sent build is
// never seen; when another machine published it first, its copy stays, and a
// rename that lost that race, which mv puts inside the winner, is deleted.
constexpr auto sendScript = std::string_view {
    "mkdir -p \"$1\" && cd \"$1\" || exit 1; "
    "staging=$(mktemp -d .staging.XXXXXX) || exit 1; "
    "if ! tar -xf - -C \"$staging\"; then rm -rf \"$staging\"; exit 1; fi; "
    "shift; status=0; "
    "for entry do "
    "if [ ! -d \"$entry\" ]; then "
    "mkdir -p \"${entry%/*}\" && mv \"$staging/$entry\" \"$entry\" 2>/dev/null; fi; "
    "if [ -d \"$entry/${entry#*/}\" ]; then rm -rf \"$entry/${entry#*/}\"; fi; "
    "if [ ! -f \"$entry/manifest.json\" ]; then status=1; fi; "
    "done; "
    "rm -rf \"$staging\"; exit $status"};

// One of the scripts above, run on the host with the arguments after the format
// directory and `input` on its stdin.
struct SshScriptRun
{
    std::string_view script;
    std::vector<std::string> arguments;
    std::string input;
    std::chrono::seconds timeLimit;
};

struct FinishedProcess
{
    int exitCode = -1;
    std::string output;
    std::string errorOutput;
};

// Kills the process when it runs past `limit`, so a slow link cannot hold up
// the configure or the send for longer.
FinishedProcess runWithTimeLimit(eacp::Processes::ProcessOptions options,
                                 const SshScriptRun& run)
{
    auto executable = options.executable;
    auto process = eacp::Processes::Process {std::move(options)};

    if (!process.launched())
        throw std::runtime_error("cannot run " + executable + "; is it on PATH?");

    auto writeInput = [&]
    {
        process.writeToInput(run.input);
        process.closeInput();
    };
    auto writer = std::thread {writeInput};
    auto deadline = std::chrono::steady_clock::now() + run.timeLimit;

    while (process.isRunning() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(processPollInterval);

    auto isPastLimit = process.isRunning();

    if (isPastLimit)
        process.kill();

    writer.join();
    auto exitCode = process.wait();

    if (isPastLimit)
        throw std::runtime_error(executable + " took longer than "
                                 + std::to_string(run.timeLimit.count())
                                 + " seconds, so it was stopped");

    return {exitCode, process.output(), process.errorOutput()};
}

// What the host's script and ssh wrote to stderr: the builds the location
// lacks, and every other line, which explains a failure.
struct SshErrorOutput
{
    std::vector<EntryAddress> locationLacks;
    eacp::Vector<std::string> otherLines;
};

SshErrorOutput parseErrorOutput(const FinishedProcess& finished)
{
    auto parsed = SshErrorOutput {};

    for (const auto& line: eacp::Strings::split(finished.errorOutput, '\n'))
    {
        auto trimmed = eacp::Strings::trim(line);
        auto lacked = std::optional<EntryAddress> {};

        if (trimmed.starts_with(lacksMarker))
            lacked = parseEntryAddress(trimmed.substr(lacksMarker.size()));

        if (lacked)
            parsed.locationLacks.push_back(*lacked);
        else if (!trimmed.empty())
            parsed.otherLines.add(trimmed);
    }

    return parsed;
}

void runCMakeTar(const eacp::FilePath& cmake,
                 const eacp::Vector<std::string>& arguments,
                 const eacp::FilePath& workingDirectory)
{
    auto options = eacp::Processes::ProcessOptions {};
    options.executable = cmake.str();
    options.arguments = {"-E", "tar"};
    options.arguments.addFrom(arguments);
    options.workingDirectory = workingDirectory.str();
    auto result = eacp::Processes::run(options);

    if (result.exitCode != 0)
        throw std::runtime_error("cmake -E tar "
                                 + eacp::Strings::join(arguments, " ") + " failed:\n"
                                 + result.errorOutput);
}

void copyEntryOutOfStore(const LibraryStore& store,
                         const EntryAddress& address,
                         const eacp::FilePath& destination)
{
    auto entry = store.getFormatDirectory() / address.key / address.buildId;
    auto copy = destination / address.key / address.buildId;

    if (!eacp::Files::copyDirectory(entry, copy))
        throw std::runtime_error("cannot copy " + entry.str() + " into "
                                 + destination.str());
}

std::string getSshDirectory(const std::string& directory)
{
    if (directory.empty() || directory == "~")
        return ".";

    if (directory.starts_with(homeDirectoryPrefix))
        return directory.substr(homeDirectoryPrefix.size());

    return directory;
}

bool isSshHostPart(const std::string& text, std::size_t colon)
{
    auto host = std::string_view {text}.substr(0, colon);
    auto isPathSeparator = [](char character)
    { return character == '/' || character == '\\'; };

    return host.size() > driveLetterLength
           && std::ranges::none_of(host, isPathSeparator);
}

std::optional<std::string> findReasonInStoredFile(const eacp::FilePath& entry,
                                                  const StoredFile& file)
{
    if (eacp::FilePath {file.name}.fileName() != file.name)
        return "its manifest names the file " + file.name
               + ", which is not a plain file name";

    auto path = entry / file.name;

    if (!eacp::Files::isFile(path))
        return "it holds no " + file.name;

    if (computeFileSha256Hex(path) != file.sha256)
        return file.name + " does not match the SHA-256 its manifest records for it";

    return std::nullopt;
}

std::optional<std::string> findReasonToDiscard(const EntryAddress& address,
                                               const eacp::FilePath& entry,
                                               const LibraryManifest& manifest)
{
    if (computeSha256Hex(manifest.inputs) != address.key)
        return "the key inputs in its manifest do not hash to its key";

    if (computeBuildId(manifest.builtWith) != address.buildId)
        return "the record of what built it does not hash to its build id";

    for (const auto& file: manifest.files)
    {
        if (auto reason = findReasonInStoredFile(entry, file))
            return reason;
    }

    return std::nullopt;
}

EntryToStore createEntryToStore(const StoredEntry& stored)
{
    return {stored.manifest.kind,
            stored.getFilePaths(),
            stored.manifest.inputs,
            stored.manifest.builtWith};
}

// Every build directory a fetch put under `staging`, as <key>/<build id>.
std::vector<EntryAddress> listFetchedBuilds(const eacp::FilePath& staging,
                                            const LibraryFetchRequest& request)
{
    auto addresses = std::vector<EntryAddress> {};
    auto keys = std::set<std::string> {};

    for (const auto& missing: request.missing)
        keys.insert(missing.key);

    for (const auto& key: keys)
    {
        for (const auto& build: eacp::Files::listDirectory(staging / key, {}))
        {
            auto address = parseEntryAddress(key + "/" + build.path.fileName());

            if (address && build.kind == eacp::Files::EntryKind::directory)
                addresses.push_back(*address);
        }
    }

    return addresses;
}
} // namespace

// How lockstep reaches a library cache location: through the file system, or
// through ssh.
class LibraryCacheTransport
{
public:
    virtual ~LibraryCacheTransport() = default;

    // Copies each build the request fetches that the location holds into
    // `destination/<key>/<build id>/`, and returns the copied builds the
    // location lacks.
    virtual std::vector<EntryAddress>
        fetch(const LibraryFetchRequest& request,
              const eacp::FilePath& destination) const = 0;

    virtual void send(const std::vector<EntryAddress>& addresses,
                      const LibraryStore& localCopies) const = 0;
};

namespace
{
class DirectoryTransport final : public LibraryCacheTransport
{
public:
    explicit DirectoryTransport(const eacp::FilePath& directory)
        : store(LibraryStoreDirectory {directory})
    {
    }

    std::vector<EntryAddress> fetch(const LibraryFetchRequest& request,
                                    const eacp::FilePath& destination) const override
    {
        for (const auto& address: request.missing)
            copyMissingBuild(address, request.fetchesOtherBuilds, destination);

        auto lacks = std::vector<EntryAddress> {};
        auto isLacked = [&](const EntryAddress& address)
        { return !store.findEntry(address); };
        std::ranges::copy_if(request.copied, std::back_inserter(lacks), isLacked);
        return lacks;
    }

    void send(const std::vector<EntryAddress>& addresses,
              const LibraryStore& localCopies) const override
    {
        for (const auto& address: addresses)
        {
            if (auto stored = localCopies.findEntry(address))
                store.storeEntry(address.key, createEntryToStore(*stored));
        }
    }

private:
    void copyMissingBuild(const EntryAddress& address,
                          bool fetchesOtherBuilds,
                          const eacp::FilePath& destination) const
    {
        if (auto stored = store.findEntry(address))
            copyEntry(*stored, destination);
        else if (fetchesOtherBuilds)
            copyEveryBuild(address.key, destination);
    }

    void copyEveryBuild(const std::string& key,
                        const eacp::FilePath& destination) const
    {
        for (const auto& stored: store.listBuilds(key))
            copyEntry(stored, destination);
    }

    static void copyEntry(const StoredEntry& stored,
                          const eacp::FilePath& destination)
    {
        auto copy = destination / stored.address.key / stored.address.buildId;
        auto isCopied = eacp::Files::copyFile(
            getLibraryManifestPath(stored.directory), getLibraryManifestPath(copy));

        for (const auto& file: stored.getFilePaths())
            isCopied =
                isCopied && eacp::Files::copyFile(file, copy / file.fileName());

        if (!isCopied)
            throw std::runtime_error("cannot copy " + stored.directory.str()
                                     + " into " + copy.str());
    }

    LibraryStore store;
};

class SshTransport final : public LibraryCacheTransport
{
public:
    SshTransport(const LibraryCacheLocation& location,
                 const eacp::FilePath& cmakeToUse,
                 const std::string& sshExecutableToUse)
        : host(location.sshHost)
        , formatDirectory(getSshDirectory(location.directory) + "/"
                          + getLibraryFormatDirectoryName())
        , cmake(cmakeToUse)
        , sshExecutable(sshExecutableToUse)
    {
    }

    std::vector<EntryAddress> fetch(const LibraryFetchRequest& request,
                                    const eacp::FilePath& destination) const override
    {
        auto input = std::string {};

        for (const auto& address: request.missing)
            input += "missing " + address.key + " " + address.buildId + "\n";

        for (const auto& address: request.copied)
            input += "copied " + address.key + " " + address.buildId + "\n";

        auto fetchesOtherBuilds = request.fetchesOtherBuilds ? "1" : "0";
        auto finished =
            runSsh({fetchScript, {fetchesOtherBuilds}, input, fetchTimeLimit});

        if (!finished.output.empty())
        {
            auto archive = destination / fetchedArchiveName;
            eacp::Files::writeFile(archive, finished.output);
            runCMakeTar(cmake, {"xf", archive.str()}, destination);
            eacp::Files::removeFile(archive);
        }

        return parseErrorOutput(finished).locationLacks;
    }

    void send(const std::vector<EntryAddress>& addresses,
              const LibraryStore& localCopies) const override
    {
        // tar runs in a copy outside the cache: Windows cannot delete a
        // directory a running process works in, so `lockstep cache clear`
        // during a send would fail.
        auto staging = eacp::TemporaryFile {};
        eacp::Files::createDirectories(staging.path());
        auto archive = eacp::TemporaryFile {".tar"};
        auto arguments = eacp::Vector<std::string> {"cf", archive.path().str()};
        auto paths = std::vector<std::string> {};

        for (const auto& address: addresses)
        {
            copyEntryOutOfStore(localCopies, address, staging.path());
            arguments.add(address.getPath());
            paths.push_back(address.getPath());
        }

        runCMakeTar(cmake, arguments, staging.path());
        runSsh({sendScript,
                paths,
                eacp::Files::readFile(archive.path()),
                sendTimeLimit});
    }

private:
    FinishedProcess runSsh(const SshScriptRun& run) const
    {
        auto commandOnHost = "sh -c '" + std::string {run.script} + "' lockstep '"
                             + formatDirectory + "'";

        for (const auto& argument: run.arguments)
            commandOnHost += " " + argument;

        auto options = eacp::Processes::ProcessOptions {};
        options.executable = sshExecutable;
        options.arguments = {
            "-T",
            "-o",
            "BatchMode=yes",
            "-o",
            "ConnectTimeout=" + std::to_string(sshConnectTimeoutSeconds),
            "-o",
            "ServerAliveInterval=" + std::to_string(sshKeepAliveIntervalSeconds),
            "-o",
            "ServerAliveCountMax="
                + std::to_string(sshKeepAliveMissesBeforeGivingUp),
            host,
            commandOnHost};
        options.workingDirectory = eacp::FilePath::tempDirectory().str();
        auto finished = runWithTimeLimit(options, run);

        if (finished.exitCode != 0)
            throw std::runtime_error(describeFailure(finished));

        return finished;
    }

    std::string describeFailure(const FinishedProcess& finished) const
    {
        auto lines = parseErrorOutput(finished).otherLines;

        if (lines.empty())
            return "ssh " + host + " exited with code "
                   + std::to_string(finished.exitCode);

        return "ssh " + host + " failed: " + eacp::Strings::join(lines, "; ");
    }

    std::string host;
    std::string formatDirectory;
    eacp::FilePath cmake;
    std::string sshExecutable;
};

std::shared_ptr<const LibraryCacheTransport>
    createTransport(const LibraryCacheLocation& location,
                    const eacp::FilePath& cmake,
                    const std::string& sshExecutable)
{
    if (location.kind == LibraryCacheLocationKind::ssh)
        return std::make_shared<SshTransport>(location, cmake, sshExecutable);

    return std::make_shared<DirectoryTransport>(eacp::FilePath {location.directory});
}
} // namespace

std::optional<LibraryCacheLocation>
    parseLibraryCacheLocation(const std::string& text)
{
    if (text.empty())
        return std::nullopt;

    auto colon = text.find(':');

    if (colon == std::string::npos || !isSshHostPart(text, colon))
        return LibraryCacheLocation {
            LibraryCacheLocationKind::directory, {}, text, text};

    auto directory = text.substr(colon + 1);

    if (directory.find('\'') != std::string::npos)
        throw std::runtime_error("the library cache location " + text
                                 + " holds a single quote, which lockstep cannot "
                                   "pass to the host's shell");

    return LibraryCacheLocation {
        LibraryCacheLocationKind::ssh, text.substr(0, colon), directory, text};
}

LibraryCacheAtLocation::LibraryCacheAtLocation(
    const LibraryCacheLocation& locationToUse,
    const eacp::FilePath& cmake,
    const std::string& sshExecutable)
    : location(locationToUse)
    , transport(createTransport(locationToUse, cmake, sshExecutable))
{
}

LibraryFetchResult
    LibraryCacheAtLocation::fetchLibraries(const LibraryFetchRequest& request,
                                           const LibraryStore& localCopies) const
{
    auto staging = eacp::TemporaryFile {".fetched"};
    eacp::Files::createDirectories(staging.path());
    auto result = LibraryFetchResult {};
    result.locationLacks = transport->fetch(request, staging.path());

    for (const auto& address: listFetchedBuilds(staging.path(), request))
    {
        auto entry = staging.path() / address.key / address.buildId;
        auto manifest = readLibraryManifest(entry);

        if (!manifest)
            continue;

        if (auto reason = findReasonToDiscard(address, entry, *manifest))
        {
            result.discarded.push_back({address, *reason});
            continue;
        }

        localCopies.storeEntry(address.key,
                               createEntryToStore({address, entry, *manifest}));
        result.fetched.insert(address);
    }

    return result;
}

void LibraryCacheAtLocation::sendLibraries(
    const std::vector<EntryAddress>& addresses,
    const LibraryStore& localCopies) const
{
    if (!addresses.empty())
        transport->send(addresses, localCopies);
}
} // namespace lockstep
