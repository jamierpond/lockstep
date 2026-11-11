# A compiler launcher that appends each compile command to RECORD_FILE and then
# runs it, so a test can see which sources a build really compiled:
# cmake -DRECORD_FILE=<file> -P RecordCompile.cmake -- <compiler> <arguments>...
# A response file's (@<file>) contents are recorded too, since NMake passes the
# source in one.

set(command)
set(is_after_separator FALSE)
math(EXPR last_argument "${CMAKE_ARGC} - 1")
foreach (index RANGE ${last_argument})
    if (is_after_separator)
        list(APPEND command "${CMAKE_ARGV${index}}")
    elseif (CMAKE_ARGV${index} STREQUAL "--")
        set(is_after_separator TRUE)
    endif ()
endforeach ()

set(record "${command}")
foreach (argument IN LISTS command)
    if (NOT argument MATCHES "^@(.+)$")
        continue()
    endif ()
    file(TO_CMAKE_PATH "${CMAKE_MATCH_1}" response_file)
    if (EXISTS "${response_file}")
        file(READ "${response_file}" response_file_text)
        string(REPLACE "\n" " " response_file_text "${response_file_text}")
        string(APPEND record " ${response_file_text}")
    endif ()
endforeach ()
file(APPEND "${RECORD_FILE}" "${record}\n")
execute_process(COMMAND ${command} RESULT_VARIABLE exit_code)
if (NOT exit_code EQUAL 0)
    message(FATAL_ERROR "the compile failed with ${exit_code}")
endif ()
