#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <map>
#include <string>
#include <vector>

namespace lockstep
{
enum class Mode
{
    locked,
    update,
    offline
};

struct CommandOptions
{
    std::string command;
    eacp::Vector<std::string> names;
    // A package's CMake arguments for add and provide; the wrapped command for
    // use-cached-library, use-cached-objects and store-libraries.
    std::vector<std::string> argumentsAfterSeparator;
    Mode mode = Mode::locked;
    bool isCI = false;
    eacp::FilePath lock;
    eacp::FilePath cache;
    eacp::FilePath cmake {"cmake"};
    eacp::FilePath sourceDirectory;
    eacp::FilePath buildDirectory;
    eacp::FilePath bootstrap;
    eacp::FilePath requestedBy;
    eacp::FilePath requestedByFile;
    eacp::FilePath requestedBinaryDirectory;
    eacp::FilePath output;
    eacp::FilePath input;
    eacp::Vector<std::string> libraries;
    // <key>=<file> of each executable, and <key>=<objects> of each object
    // library, that store-libraries stores after the link.
    eacp::Vector<std::string> executables;
    eacp::Vector<std::string> objectLibraries;
    // The stored object files use-cached-objects copies, in the order of the
    // ;-list in `objects`, $<TARGET_OBJECTS:...> of the object library.
    eacp::Vector<std::string> storedObjects;
    std::string objects;
    // The object file the compile use-cached-objects stands in for writes.
    std::string object;
    // LOCKSTEP_LIBRARY_CACHE_LOCATION: a directory or [user@]host:directory.
    std::string libraryCacheLocation;
    // The size limits in bytes of this machine's library cache and of the
    // location, which the configure passes to the commands the build runs.
    std::string libraryCacheMaxSize;
    std::string libraryCacheLocationMaxSize;
    // <name>=<value> of every library cache setting defined as a cache variable
    // on the build directory, even an empty one.
    eacp::Vector<std::string> cacheVariables;
    // The library cache settings' environment variables, read at start-up.
    std::map<std::string, std::string> settingEnvironment;
    std::string topLevelIncludes;
    eacp::Vector<std::string> overrides;
    bool isFullVerify = false;
    bool shouldAnnounce = false;
};

int runBeginCommand(const CommandOptions& options);
int runAddCommand(const CommandOptions& options);
int runProvideCommand(const CommandOptions& options);
int runUpdateCommand(const CommandOptions& options);
int runVerifyCommand(const CommandOptions& options);
int runRootCommand(const CommandOptions& options);
int runCMakeModuleCommand(const CommandOptions& options);
int runLibraryCacheCommand(const CommandOptions& options);
int runUseCachedLibraryCommand(const CommandOptions& options);
int runUseCachedObjectsCommand(const CommandOptions& options);
int runStoreLibrariesCommand(const CommandOptions& options);
int runSendLibrariesCommand(const CommandOptions& options);
int runCacheCommand(const CommandOptions& options);
} // namespace lockstep
