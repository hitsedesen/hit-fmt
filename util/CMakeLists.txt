add_library(lib-util STATIC
    dummy.cpp)
target_include_directories(lib-util
    PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(lib-util
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
