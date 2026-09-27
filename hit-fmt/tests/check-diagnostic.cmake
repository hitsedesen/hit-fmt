set(ENV{COLUMNS} 120)
set(input "${TESTDATA}/input.cpp")
execute_process(
    COMMAND "${HIT_FMT}" --check --config "${TESTDATA}/style.hit-fmt" "${input}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE actual
    ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT errors STREQUAL "")
    message(FATAL_ERROR "Unexpected check result: ${status}\n${errors}")
endif()
string(REPLACE "${input}" "<input>" actual "${actual}")
file(READ "${TESTDATA}/output.expected" expected)
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Diagnostic mismatch:\n${actual}\nExpected:\n${expected}")
endif()
