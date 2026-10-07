cmake_minimum_required(VERSION 3.20)

project(hit-fmt
    VERSION 0.8.0
    DESCRIPTION "Configurable C++ formatter"
    HOMEPAGE_URL "https://sourcecraft.dev/hitsedesen/hit-fmt"
    LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HIT_FMT_TEST "Build tests" OFF)
option(HIT_FMT_BENCHMARK "Build benchmarks" OFF)

if(HIT_FMT_TEST)
    find_package(GTest CONFIG REQUIRED)
endif()

if(HIT_FMT_BENCHMARK)
    find_package(benchmark CONFIG REQUIRED)
    add_subdirectory(benchmarks)
endif()

add_subdirectory(util)
add_subdirectory(unicode)
add_subdirectory(tokenizer)
add_subdirectory(parser)
add_subdirectory(document)
add_subdirectory(formatter)
add_subdirectory(hit-fmt)

if(HIT_FMT_TEST)
    add_subdirectory(tests)
endif()

install(FILES
    LICENSE
    LICENSES/Unicode-3.0.txt
    DESTINATION ${CMAKE_INSTALL_DOCDIR})
