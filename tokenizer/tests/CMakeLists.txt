add_executable(fmt_tests tests.cpp)
target_link_libraries(fmt_tests PRIVATE
    lib-tokenizer
    GTest::gtest_main)

gtest_discover_tests(fmt_tests)

add_executable(tokenizer_allocation_test allocation_test.cpp)
target_link_libraries(tokenizer_allocation_test PRIVATE lib-tokenizer)
add_test(NAME tokenizer_allocation_test COMMAND tokenizer_allocation_test)

add_custom_command(
    TARGET fmt_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata
            ${CMAKE_CURRENT_BINARY_DIR}/testdata
)
