#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

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
    std::vector<std::string> packageArguments;
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
} // namespace lockstep
