#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <string>

namespace lockstep
{
struct CommandOptions
{
    std::string command;
    eacp::Vector<std::string> names;
    std::string name;
    std::string kind = "git";
    std::string url;
    std::string ref;
    eacp::Vector<std::string> patches;
    std::string requester;
    std::string mode = "locked";
    eacp::FilePath lock {"lockstep.lock"};
    eacp::FilePath cache;
    eacp::FilePath cmake {"cmake"};
    eacp::FilePath shim;
    bool full = false;
};

int runResolveCommand(const CommandOptions& options);
int runUpdateCommand(const CommandOptions& options);
int runVerifyCommand(const CommandOptions& options);
int runRootCommand(const CommandOptions& options);
} // namespace lockstep
