function(embed_text source output symbol)
    set(input "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
    file(READ "${input}" text)
    string(REPLACE "\r\n" "\n" text "${text}")
    # Keep individual generated literals below MSVC's length limit.
    string(REPLACE "\n" "\n)text\"\nR\"text(" text_literals "${text}")
    configure_file(embedded_text.hpp.in ${output} @ONLY)
endfunction()

embed_text(config_help.md config_help_data.hpp ConfigHelpText)
embed_text(config_template.hit-fmt config_template_data.hpp ConfigTemplateText)
embed_text(examples/ytsaurus.hit-fmt ytsaurus_config_data.hpp YTsaurusConfigText)
embed_text(examples/google.hit-fmt google_config_data.hpp GoogleConfigText)
embed_text(examples/llvm.hit-fmt llvm_config_data.hpp LLVMConfigText)

add_library(lib-file-check STATIC
    file_check.cpp
    config_help.cpp
    options.cpp
    source_excerpt.cpp
    formatting_diagnostics.cpp
    terminal.cpp
    config_template.cpp)
target_link_libraries(lib-file-check PUBLIC lib-formatter)
target_include_directories(lib-file-check PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_executable(hit-fmt
    main.cpp
    run.cpp
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
