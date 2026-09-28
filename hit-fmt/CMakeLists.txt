add_executable(hit-fmt
    main.cpp)
target_link_libraries(hit-fmt
    lib-formatter
    lib-util)
target_include_directories(hit-fmt
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(HIT_FMT_TEST)
    foreach(case separator-diagnostic separator-indent call-specific-limit
        repeated-lambda-call)
        add_test(NAME hit-fmt-${case}
            COMMAND ${CMAKE_COMMAND}
                -DHIT_FMT=$<TARGET_FILE:hit-fmt>
                -DTESTDATA=${CMAKE_CURRENT_SOURCE_DIR}/tests/testdata/${case}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-diagnostic.cmake)
    endforeach()
endif()

install(TARGETS hit-fmt
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
