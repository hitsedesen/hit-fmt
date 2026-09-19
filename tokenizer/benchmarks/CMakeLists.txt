add_executable(tokenizer-benchmark
    tokenizer_benchmark.cpp)
target_link_libraries(tokenizer-benchmark PRIVATE
    benchmark::benchmark
    benchmark-inputs
    lib-tokenizer)
