add_executable(formatter-benchmark
    formatter_benchmark.cpp)
target_link_libraries(formatter-benchmark PRIVATE
    benchmark::benchmark
    benchmark-inputs
    lib-formatter)
