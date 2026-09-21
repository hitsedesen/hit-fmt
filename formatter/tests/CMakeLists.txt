add_executable(formatter-tests
    golden_tests.cpp)
target_link_libraries(formatter-tests
    GTest::gtest_main
    lib-formatter)
target_compile_definitions(formatter-tests PRIVATE
    FORMATTER_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata/style")

gtest_discover_tests(formatter-tests)
