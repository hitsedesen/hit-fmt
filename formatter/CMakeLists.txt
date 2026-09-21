add_library(lib-formatter STATIC
    config.cpp
    formatter.cpp)
target_include_directories(lib-formatter
    PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(lib-formatter
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(lib-formatter
    lib-parser)

if(HIT_FMT_TEST)
    add_subdirectory(tests)
endif(HIT_FMT_TEST)

if(HIT_FMT_BENCHMARK)
    add_subdirectory(benchmarks)
endif(HIT_FMT_BENCHMARK)
