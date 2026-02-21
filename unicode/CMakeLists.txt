add_library(lib-unicode
    code_point.cpp
    unicode_data.cpp)
target_include_directories(lib-unicode
    PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(lib-unicode
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lib-unicode
    lib-util)
