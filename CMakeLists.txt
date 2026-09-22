cmake_minimum_required(VERSION 3.20)

project(hit-fmt
    VERSION 0
    DESCRIPTION "hitsedesen's cpp formatter"
    HOMEPAGE_URL "https://sourcecraft.dev/hitsedesen/hit-fmt"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HIT_FMT_TEST "Build tests" OFF)
option(HIT_FMT_BENCHMARK "Build benchmarks" OFF)

if(HIT_FMT_TEST)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)
endif()

if(HIT_FMT_BENCHMARK)
    find_package(benchmark CONFIG REQUIRED)
    add_subdirectory(benchmarks)
endif()

add_subdirectory(util)
add_subdirectory(unicode)
add_subdirectory(tokenizer)
add_subdirectory(parser)
add_subdirectory(formatter)
add_subdirectory(hit-fmt)
