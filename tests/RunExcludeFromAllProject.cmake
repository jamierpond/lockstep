# Builds a project with no --target and installs it. The package added without
# EXCLUDE_FROM_ALL keeps its unlinked target and install rules out; the one
# added with EXCLUDE_FROM_ALL NO builds and installs everything it declares.
# Expects PROJECT_DIR, WORK_DIR, LOCKSTEP_EXECUTABLE and LOCKSTEP_SHIM.

function(run_step)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE exit_code
            OUTPUT_VARIABLE output ERROR_VARIABLE output)
    message("${output}")
    if (NOT exit_code EQUAL 0)
        message(FATAL_ERROR "step failed: ${ARGN}")
    endif ()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(COPY "${PROJECT_DIR}/" DESTINATION "${WORK_DIR}/source")
file(COPY "${PROJECT_DIR}/../github/main.cpp" DESTINATION "${WORK_DIR}/github")

set(build "${WORK_DIR}/build")
set(prefix "${WORK_DIR}/prefix")
run_step("${CMAKE_COMMAND}" -S "${WORK_DIR}/source" -B "${build}"
        "-DLOCKSTEP_SHIM=${LOCKSTEP_SHIM}"
        "-DLOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
        "-DLOCKSTEP_CACHE=${WORK_DIR}/cache"
        -DCMAKE_BUILD_TYPE=Release)
run_step("${CMAKE_COMMAND}" --build "${build}" --config Release)
run_step("${CMAKE_COMMAND}" --install "${build}" --config Release --prefix "${prefix}")

if (EXISTS "${prefix}/excluded")
    message(FATAL_ERROR "the install ran the rules of a package excluded from all")
endif ()

find_program(tool NAMES included-tool PATHS "${prefix}/included" NO_DEFAULT_PATH)
if (NOT tool)
    message(FATAL_ERROR "EXCLUDE_FROM_ALL NO did not build and install included-tool")
endif ()
