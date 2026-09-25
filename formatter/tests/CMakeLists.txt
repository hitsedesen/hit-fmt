add_executable(formatter-tests
    golden_tests.cpp)
target_link_libraries(formatter-tests
    GTest::gtest_main
    lib-formatter)
target_compile_definitions(formatter-tests PRIVATE
    FORMATTER_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata/style"
    BRACE_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata/brace-placement"
    BRACE_SPACING_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata/brace-spacing"
    LAMBDA_SPACING_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata/lambda-spacing"
    LAMBDA_CAPTURE_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata/lambda-captures"
    CONTROL_SPACING_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata/control-keyword-spacing"
    FUNCTION_SPACING_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata/function-spacing"
    FUNCTION_CALL_SPACING_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata/function-call-spacing"
    CALL_INNER_SPACING_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata/call-inner-spacing"
    FUNCTION_INNER_SPACING_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata/function-inner-spacing"
    CONTROL_BRACE_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata/control-brace-placement"
    NAMING_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata/naming")

gtest_discover_tests(formatter-tests)
