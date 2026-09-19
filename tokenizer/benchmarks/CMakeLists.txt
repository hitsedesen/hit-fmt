find_package(benchmark CONFIG REQUIRED)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/inputs/service.cpp"
    TOKENIZER_BENCHMARK_SERVICE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/inputs/template_library.hpp"
    TOKENIZER_BENCHMARK_TEMPLATE_LIBRARY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/inputs/platform.hpp"
    TOKENIZER_BENCHMARK_PLATFORM)
configure_file(
    benchmark_inputs.hpp.in
    benchmark_inputs.hpp
    @ONLY)

add_executable(tokenizer-benchmark
    tokenizer_benchmark.cpp)
target_include_directories(tokenizer-benchmark PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(tokenizer-benchmark PRIVATE
    benchmark::benchmark
    lib-tokenizer)
