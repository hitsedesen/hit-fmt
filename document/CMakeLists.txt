add_library(lib-document STATIC
    document.cpp)
target_include_directories(lib-document
    PUBLIC ${CMAKE_SOURCE_DIR})
