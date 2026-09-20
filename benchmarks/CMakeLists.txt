file(READ "${CMAKE_CURRENT_SOURCE_DIR}/inputs/service.cpp"
    HIT_FMT_BENCHMARK_SERVICE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/inputs/template_library.hpp"
    HIT_FMT_BENCHMARK_TEMPLATE_LIBRARY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/inputs/platform.hpp"
    HIT_FMT_BENCHMARK_PLATFORM)
configure_file(
    benchmark_inputs.hpp.in
    benchmark_inputs.hpp
    @ONLY)

add_library(benchmark-inputs INTERFACE)
target_include_directories(benchmark-inputs INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR})
