add_executable(parser-benchmark
    parser_benchmark.cpp)
target_link_libraries(parser-benchmark PRIVATE
    benchmark::benchmark
    benchmark-inputs
    lib-parser)

add_executable(parser-allocation-benchmark
    allocation_benchmark.cpp)
target_link_libraries(parser-allocation-benchmark PRIVATE
    benchmark::benchmark)
