cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${LOCKSTEP_SHIM})
project(GitEdgeCasesSample LANGUAGES NONE)

lockstep_add_package(NAME emptyModules GIT_REPOSITORY "${FIXTURE_URL}/emptyModules.git"
        GIT_TAG main DOWNLOAD_ONLY)
lockstep_add_package(NAME danglingModule GIT_REPOSITORY "${FIXTURE_URL}/danglingModule.git"
        GIT_TAG main DOWNLOAD_ONLY)
lockstep_add_package(NAME lfs GIT_REPOSITORY "${FIXTURE_URL}/lfs.git" GIT_TAG main
        DOWNLOAD_ONLY)

file(READ "${lfs_SOURCE_DIR}/payload.bin" payload)
if (NOT payload STREQUAL "${LFS_PAYLOAD}")
    message(FATAL_ERROR "payload.bin is not the lfs content:\n${payload}")
endif ()
