add_library(lib-formatter STATIC
    config.cpp
    formatter.cpp
    hunk_format.cpp
    hunks.cpp
    include_policy.cpp
    layout.cpp
    naming.cpp
    parameter_names.cpp
    policy.cpp
    source_analysis.cpp
    structural_edits.cpp
    syntax_layout.cpp
    syntax_spacing.cpp)
target_include_directories(lib-formatter
    PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(lib-formatter
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(lib-formatter
    lib-parser)

if(HIT_FMT_BENCHMARK)
    add_subdirectory(benchmarks)
endif()
