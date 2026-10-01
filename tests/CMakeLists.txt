add_executable(tests
    ../tokenizer/tests/tests.cpp
    ../parser/tests/tests.cpp
    ../formatter/tests/hunks.cpp
    ../formatter/tests/tests.cpp
    ../hit-fmt/tests/file_batch.cpp)
target_link_libraries(tests PRIVATE
    GTest::gtest_main
    lib-formatter)
target_compile_definitions(tests PRIVATE
    TOKENIZER_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/tokenizer/tests/testdata"
    PARSER_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/parser/tests/testdata"
    FORMATTER_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/style"
    BRACE_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/brace-placement"
    BRACE_SPACING_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/brace-spacing"
    LAMBDA_SPACING_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/lambda-spacing"
    LAMBDA_CAPTURE_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/lambda-captures"
    LAMBDA_INNER_PARAMETER_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/lambda-inner-parameters"
    BODY_INDENT_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/body-indent"
    INHERITANCE_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/inheritance"
    CONTROL_SPACING_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/control-keyword-spacing"
    FUNCTION_SPACING_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/function-spacing"
    FUNCTION_CALL_SPACING_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/function-call-spacing"
    CALL_INNER_SPACING_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/call-inner-spacing"
    FUNCTION_INNER_SPACING_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/function-inner-spacing"
    CONTROL_BRACE_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/control-brace-placement"
    NAMING_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/naming")

add_executable(allocation_tests
    ../tokenizer/tests/allocation_test.cpp)
target_link_libraries(allocation_tests PRIVATE lib-tokenizer)
