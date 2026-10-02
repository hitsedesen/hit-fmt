find_package(Threads REQUIRED)

add_library(lib-file-check STATIC file_check.cpp)
target_link_libraries(lib-file-check PUBLIC lib-formatter)

add_executable(hit-fmt
    main.cpp
    config_help.cpp
    config_template.cpp
    version.cpp)
target_link_libraries(hit-fmt
    lib-file-check
    lib-util
    Threads::Threads)
# Refresh the embedded version on each build, even if only Git state changes.
set(version_header "${CMAKE_CURRENT_BINARY_DIR}/build_version.hpp")
add_custom_target(hit-fmt-version-header
    COMMAND "${CMAKE_COMMAND}"
        "-DPROJECT_VERSION=${PROJECT_VERSION}"
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DBINARY_DIR=${PROJECT_BINARY_DIR}"
        "-DOUTPUT_FILE=${version_header}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/build_version.cmake"
    BYPRODUCTS "${version_header}"
    VERBATIM)
add_dependencies(hit-fmt hit-fmt-version-header)

target_include_directories(hit-fmt
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

install(TARGETS hit-fmt
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
