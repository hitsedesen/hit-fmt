cmake_minimum_required(VERSION 3.20)

project(hit-fmt
    VERSION 0
    DESCRIPTION "hitsedesen's cpp formatter"
    HOMEPAGE_URL "https://sourcecraft.dev/hitsedesen/hit-fmt"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HIT_FMT_TEST NO)
option(HIT_FMT_BENCHMARK "Build benchmarks" OFF)

if(HIT_FMT_TEST)
    enable_testing()
    add_subdirectory(external/googletest)
    include(GoogleTest)
endif(HIT_FMT_TEST)

add_subdirectory(util)
add_subdirectory(unicode)
add_subdirectory(tokenizer)
add_subdirectory(hit-fmt)
