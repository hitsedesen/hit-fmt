add_executable(tests
    ../tokenizer/tests/tests.cpp
    ../parser/tests/tests.cpp
    ../formatter/tests/hunks.cpp
    ../formatter/tests/layout.cpp
    ../formatter/tests/source_analysis_tests.cpp
    ../formatter/tests/structural_edits_tests.cpp
    ../formatter/tests/tests.cpp
    ../hit-fmt/tests/file_batch.cpp
    ../hit-fmt/tests/file_dispatch.cpp
    ../hit-fmt/tests/file_check.cpp)
target_link_libraries(tests PRIVATE
    GTest::gtest_main
    lib-file-check
    Threads::Threads)
target_compile_definitions(tests PRIVATE
    TOKENIZER_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/tokenizer/tests/testdata"
    PARSER_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/parser/tests/testdata"
    FORMATTER_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/style"
    GROUP_LAYOUT_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/group-layout"
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

set(public_pipeline_sources
    ../formatter/tests/config_policy.cpp
    ../formatter/tests/public_pipeline.cpp
    ../formatter/tests/syntax_layout.cpp
    ../formatter/tests/syntax_spacing_tests.cpp
    ../hit-fmt/config_template.cpp)
target_sources(tests PRIVATE ${public_pipeline_sources})
add_executable(pipeline_checks
    ../parser/tests/tests.cpp
    ../formatter/tests/layout.cpp
    ../formatter/tests/source_analysis_tests.cpp
    ../formatter/tests/structural_edits_tests.cpp
    ${public_pipeline_sources})
target_link_libraries(pipeline_checks PRIVATE GTest::gtest_main lib-formatter)
target_compile_definitions(pipeline_checks PRIVATE
    HIT_FMT_PIPELINE_CHECKS=1
    PARSER_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/parser/tests/testdata"
    FORMATTER_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/style"
    GROUP_LAYOUT_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/group-layout")

# CLI checks exercise the library without launching the executable.
add_executable(cli_checks
    ../hit-fmt/tests/file_batch.cpp
    ../hit-fmt/tests/file_dispatch.cpp
    ../hit-fmt/tests/file_check.cpp)
target_link_libraries(cli_checks PRIVATE GTest::gtest_main lib-file-check Threads::Threads)

add_executable(hunk_checks
    ../formatter/tests/hunks.cpp
    ../formatter/tests/hunk_scope.cpp)
target_sources(tests PRIVATE ../formatter/tests/hunk_scope.cpp)
target_link_libraries(hunk_checks PRIVATE GTest::gtest_main lib-formatter)
target_compile_definitions(hunk_checks PRIVATE
    HIT_FMT_HUNK_CHECKS=1
    HUNK_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/formatter/tests/testdata/changed-hunks")

add_executable(hunk_scope_checks ../formatter/tests/hunk_scope.cpp)
target_link_libraries(hunk_scope_checks PRIVATE GTest::gtest_main lib-formatter)
