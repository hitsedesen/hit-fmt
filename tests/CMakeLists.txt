add_executable(tests
    ../tokenizer/tests/tests.cpp
    ../parser/tests/tests.cpp
    ../formatter/tests/hunks.cpp
    ../formatter/tests/hunk_scope.cpp
    ../document/tests/tests.cpp
    ../formatter/tests/source_analysis_tests.cpp
    ../formatter/tests/structural_edits_tests.cpp
    ../formatter/tests/tests.cpp
    ../formatter/tests/config_policy.cpp
    ../formatter/tests/public_pipeline.cpp
    ../formatter/tests/syntax_layout.cpp
    ../formatter/tests/syntax_spacing_tests.cpp
    ../hit-fmt/tests/file_batch.cpp
    ../hit-fmt/tests/file_dispatch.cpp
    ../hit-fmt/tests/file_check.cpp
    ../hit-fmt/tests/config_help.cpp)
target_link_libraries(tests PRIVATE
    GTest::gtest_main
    lib-file-check
    Threads::Threads)
target_compile_definitions(tests PRIVATE
    HIT_FMT_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/hit-fmt/tests/testdata"
    TOKENIZER_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/tokenizer/tests/testdata"
    PARSER_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/parser/tests/testdata"
    FORMATTER_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/style"
    DOCUMENT_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/document/tests/testdata/group-layout"
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
