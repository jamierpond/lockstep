CPMAddPackage(
        NAME NanoTest
        GITHUB_REPOSITORY eyalamirmusic/NanoTest
        GIT_TAG main)

nano_add_executable(LockstepTests
        SOURCES Sha256Tests.cpp MerkleHashTests.cpp LockFileTests.cpp PinnedReleaseTests.cpp PackageArgumentsTests.cpp
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
add_consumer_project_test(submodules 1)
