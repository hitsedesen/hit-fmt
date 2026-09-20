add_executable(parser-tests
    tests.cpp)
target_link_libraries(parser-tests
    GTest::gmock_main
    lib-parser)

gtest_discover_tests(parser-tests)

add_custom_command(
    TARGET parser-tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata
            ${CMAKE_CURRENT_BINARY_DIR}/testdata)
