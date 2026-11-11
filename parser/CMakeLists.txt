add_library(lib-parser STATIC
    parser.cpp
    delimiters.cpp
    delimiter_block.cpp)
target_include_directories(lib-parser
    PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(lib-parser
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(lib-parser
    lib-tokenizer
    lib-util)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|i.86|x86)$"
        AND (NOT MSVC OR CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x64)$"))
    include(CheckCXXCompilerFlag)
    function(add_delimiter_classifier name suffix width)
        string(JOIN " " flags ${ARGN})
        check_cxx_compiler_flag("${flags}" HIT_FMT_COMPILER_HAS_DELIMITER_${name})
        if(HIT_FMT_COMPILER_HAS_DELIMITER_${name})
            add_library(parser-delimiters-${name} OBJECT delimiter_block_x86.cpp)
            target_include_directories(parser-delimiters-${name} PRIVATE ${CMAKE_SOURCE_DIR})
            target_compile_options(parser-delimiters-${name} PRIVATE ${ARGN})
            target_compile_definitions(parser-delimiters-${name} PRIVATE
                HIT_FMT_DELIMITER_CLASSIFIER=ClassifyDelimiterBlock${suffix}
                HIT_FMT_DELIMITER_WIDTH=${width})
            target_sources(lib-parser PRIVATE $<TARGET_OBJECTS:parser-delimiters-${name}>)
            target_compile_definitions(lib-parser PRIVATE HIT_FMT_DELIMITER_${name}=1)
        endif()
    endfunction()
    if(MSVC)
        add_delimiter_classifier(SSE2 Sse2 16 /arch:SSE2)
        add_delimiter_classifier(AVX Avx 16 /arch:AVX)
        add_delimiter_classifier(AVX2 Avx2 32 /arch:AVX2)
        add_delimiter_classifier(AVX512 Avx512 64 /arch:AVX512)
    else()
        add_delimiter_classifier(SSE2 Sse2 16 -msse2)
        add_delimiter_classifier(AVX Avx 16 -mavx)
        add_delimiter_classifier(AVX2 Avx2 32 -mavx2)
        add_delimiter_classifier(AVX512 Avx512 64 -mavx512f -mavx512bw)
    endif()
endif()
