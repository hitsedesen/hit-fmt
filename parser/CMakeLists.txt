add_library(lib-parser STATIC
    parser.cpp)
target_include_directories(lib-parser
    PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(lib-parser
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(lib-parser
    lib-tokenizer
    lib-util)

if(HIT_FMT_TEST)
    add_subdirectory(tests)
endif()

if(HIT_FMT_BENCHMARK)
    add_subdirectory(benchmarks)
endif()
