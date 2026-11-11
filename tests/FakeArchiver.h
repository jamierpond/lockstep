#pragma once

#include <string_view>

// The test binary, copied in as the cmake a send runs, is started as
// `cmake -E tar ...`. It writes its working directory to archiver-started.txt
// beside itself, waits for archiver-release.txt there, and fails, so a test
// can act while a send is archiving.
constexpr auto fakeArchiverMode = std::string_view {"-E"};
constexpr auto fakeArchiverStartedName = "archiver-started.txt";
constexpr auto fakeArchiverReleaseName = "archiver-release.txt";

int runFakeArchiver();
