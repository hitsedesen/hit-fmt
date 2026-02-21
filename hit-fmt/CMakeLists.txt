add_executable(hit-fmt
    main.cpp)
target_link_libraries(hit-fmt
    lib-util
    lib-unicode
    lib-tokenizer)
target_include_directories(hit-fmt
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
