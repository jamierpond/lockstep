lockstep_add_package(NAME NanoTest GITHUB eyalamirmusic/NanoTest GIT_TAG main)

nano_add_executable(LockstepTests
        SOURCES Main.cpp Sha256Tests.cpp MerkleHashTests.cpp LockFileTests.cpp PinnedReleaseTests.cpp PackageArgumentsTests.cpp MessagesTests.cpp LibraryCacheTests.cpp
        TARGETS lockstep-lib
        NO_MAIN)
target_compile_definitions(LockstepTests PRIVATE
        LOCKSTEP_TESTS_EXECUTABLE="$<TARGET_FILE:LockstepTests>")

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
add_consumer_project_test(patch-beside-file 1)
add_consumer_project_test(cpm-compat 2)

add_test(NAME Consumer/submodules
        COMMAND ${CMAKE_COMMAND}
        -DPROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/projects/submodules
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer-submodules
        -DLOCKSTEP_EXECUTABLE=$<TARGET_FILE:lockstep>
        -DLOCKSTEP_SHIM=${PROJECT_SOURCE_DIR}/cmake/lockstep.cmake
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunSubmoduleProject.cmake)

add_test(NAME Consumer/excludeFromAll
        COMMAND ${CMAKE_COMMAND}
        -DPROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/projects/exclude-from-all
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer-exclude-from-all
        -DLOCKSTEP_EXECUTABLE=$<TARGET_FILE:lockstep>
        -DLOCKSTEP_SHIM=${PROJECT_SOURCE_DIR}/cmake/lockstep.cmake
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunExcludeFromAllProject.cmake)

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

# The library cache handles single-configuration generators only.
get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT is_multi_config)
    add_test(NAME Consumer/libraryCache
            COMMAND ${CMAKE_COMMAND}
            -DPROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/projects/library-cache
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer-library-cache
            -DLOCKSTEP_EXECUTABLE=$<TARGET_FILE:lockstep>
            -DLOCKSTEP_SHIM=${PROJECT_SOURCE_DIR}/cmake/lockstep.cmake
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DMSVC_ABI=${MSVC}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunLibraryCacheProject.cmake)

    function(add_library_cache_location_test name)
        add_test(NAME Consumer/${name}
                COMMAND ${CMAKE_COMMAND}
                -DPROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/projects/library-cache
                -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer-${name}
                -DLOCKSTEP_EXECUTABLE=$<TARGET_FILE:lockstep>
                -DLOCKSTEP_SHIM=${PROJECT_SOURCE_DIR}/cmake/lockstep.cmake
                "-DGENERATOR=${CMAKE_GENERATOR}"
                ${ARGN}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/RunLibraryCacheLocationProject.cmake)
    endfunction()

    add_library_cache_location_test(libraryCacheLocation)

    # Reported as skipped where `ssh -o BatchMode=yes <host> true` fails.
    set(LOCKSTEP_TESTS_SSH_HOST localhost CACHE STRING
            "The host Consumer/libraryCacheOverSsh keeps a library cache location on")
    add_library_cache_location_test(libraryCacheOverSsh "-DSSH_HOST=${LOCKSTEP_TESTS_SSH_HOST}")
    set_tests_properties(Consumer/libraryCacheOverSsh PROPERTIES
            SKIP_REGULAR_EXPRESSION "lockstep-test-skipped")
endif ()
