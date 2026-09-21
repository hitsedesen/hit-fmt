add_executable(hit-fmt
    main.cpp)
target_link_libraries(hit-fmt
    lib-formatter
    lib-util)
target_include_directories(hit-fmt
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
