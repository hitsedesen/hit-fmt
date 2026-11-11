add_library(lib-fs STATIC fs.cpp path.cpp)
target_include_directories(lib-fs PUBLIC ${CMAKE_SOURCE_DIR})
