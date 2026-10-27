add_library(lib-tokenizer STATIC
    ascii_block.cpp
    tokenizer.cpp)
target_include_directories(lib-tokenizer
    PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(lib-tokenizer
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(lib-tokenizer
    lib-util
    lib-unicode)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|i.86|x86)$"
        AND (NOT MSVC OR CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x64)$"))
    include(CheckCXXCompilerFlag)
    if(MSVC)
        set(HIT_FMT_AVX2_FLAG /arch:AVX2)
    else()
        set(HIT_FMT_AVX2_FLAG -mavx2)
    endif()
    check_cxx_compiler_flag("${HIT_FMT_AVX2_FLAG}" HIT_FMT_COMPILER_HAS_AVX2)
    if(HIT_FMT_COMPILER_HAS_AVX2)
        target_sources(lib-tokenizer PRIVATE ascii_block_avx2.cpp)
        set_source_files_properties(ascii_block_avx2.cpp PROPERTIES
            COMPILE_OPTIONS "${HIT_FMT_AVX2_FLAG}")
        target_compile_definitions(lib-tokenizer PRIVATE HIT_FMT_HAS_AVX2=1)
    endif()
endif()
