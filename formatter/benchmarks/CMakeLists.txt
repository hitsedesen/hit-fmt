add_executable(formatter-corpus-benchmark
    corpus_benchmark.cpp)
target_link_libraries(formatter-corpus-benchmark PRIVATE
    lib-formatter)
