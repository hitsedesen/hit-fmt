add_library(lib-tokenizer STATIC
    tokenizer.cpp)
target_include_directories(lib-tokenizer
    PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(lib-tokenizer
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(lib-tokenizer
    lib-util
    lib-unicode)

if(HIT_FMT_TEST)
add_subdirectory(tests)
endif(HIT_FMT_TEST)

if(HIT_FMT_BENCHMARK)
    add_subdirectory(benchmarks)
endif(HIT_FMT_BENCHMARK)
