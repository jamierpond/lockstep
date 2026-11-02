lockstep_add_package(NAME NanoTest GITHUB eyalamirmusic/NanoTest GIT_TAG main)

nano_add_executable(LockstepTests
        SOURCES Sha256Tests.cpp MerkleHashTests.cpp LockFileTests.cpp PinnedReleaseTests.cpp PackageArgumentsTests.cpp MessagesTests.cpp
        TARGETS lockstep-lib)

function(add_consumer_project_test name expected_packages)
    add_test(NAME Consumer/${name}
            COMMAND ${CMAKE_COMMAND}
            -DPROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/projects/${name}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer-${name}
            -DLOCKSTEP_EXECUTABLE=$<TARGET_FILE:lockstep>
            -DLOCKSTEP_SHIM=${PROJECT_SOURCE_DIR}/cmake/lockstep.cmake
            -DEXPECTED_PACKAGES=${expected_packages}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunConsumerProject.cmake)
endfunction()

add_consumer_project_test(github 1)
add_consumer_project_test(cpm 4)
add_consumer_project_test(fetchcontent 1)
add_consumer_project_test(archive 2)
add_consumer_project_test(cpm-stub 1)
add_consumer_project_test(cpm-compat 2)

add_test(NAME Consumer/submodules
        COMMAND ${CMAKE_COMMAND}
        -DPROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/projects/submodules
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer-submodules
        -DLOCKSTEP_EXECUTABLE=$<TARGET_FILE:lockstep>
        -DLOCKSTEP_SHIM=${PROJECT_SOURCE_DIR}/cmake/lockstep.cmake
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSubmoduleProject.cmake)

add_test(NAME Consumer/git-edge-cases
        COMMAND ${CMAKE_COMMAND}
        -DPROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/projects/git-edge-cases
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer-git-edge-cases
        -DLOCKSTEP_EXECUTABLE=$<TARGET_FILE:lockstep>
        -DLOCKSTEP_SHIM=${PROJECT_SOURCE_DIR}/cmake/lockstep.cmake
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunGitEdgeCasesProject.cmake)

# Windows is left out: lockstep's cache root there is the known folder, which
# no environment variable can point at a scratch directory.
if (NOT WIN32)
    add_test(NAME Bootstrap/cacheLayoutMatchesLockstep
            COMMAND ${CMAKE_COMMAND}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-cache
            -DLOCKSTEP_EXECUTABLE=$<TARGET_FILE:lockstep>
            -DLOCKSTEP_SHIM=${PROJECT_SOURCE_DIR}/cmake/lockstep.cmake
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunBootstrapCacheProject.cmake)
endif ()

add_test(NAME Consumer/archiveWithWrongHash
        COMMAND ${CMAKE_COMMAND}
        -DPROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/projects/archive-bad-hash
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer-archive-bad-hash
        -DLOCKSTEP_EXECUTABLE=$<TARGET_FILE:lockstep>
        -DLOCKSTEP_SHIM=${PROJECT_SOURCE_DIR}/cmake/lockstep.cmake
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunBadHashProject.cmake)
