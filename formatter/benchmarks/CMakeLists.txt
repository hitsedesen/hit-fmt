add_executable(formatter-benchmark
    formatter_benchmark.cpp)
target_link_libraries(formatter-benchmark PRIVATE
    benchmark::benchmark
    benchmark-inputs
    lib-formatter)

add_executable(formatter-corpus-benchmark
    corpus_benchmark.cpp)
target_link_libraries(formatter-corpus-benchmark PRIVATE
    lib-formatter)
