# A checkout at the matching tag is a release only if it is clean.
set(build_version "${PROJECT_VERSION}-dev")
find_program(GIT_EXECUTABLE git)
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE revision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE revision_result)
    if(revision_result EQUAL 0)
        set(build_version "${build_version}+${revision}")

        # An in-tree build directory is not a source change, even if unignored.
        set(status_paths .)
        file(RELATIVE_PATH relative_build_dir "${SOURCE_DIR}" "${BINARY_DIR}")
        if(NOT relative_build_dir MATCHES "^\\.\\." AND
            NOT relative_build_dir STREQUAL "." AND
            NOT relative_build_dir STREQUAL "")
            list(APPEND status_paths ":(exclude)${relative_build_dir}")
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain
                --untracked-files=normal -- ${status_paths}
            WORKING_DIRECTORY "${SOURCE_DIR}"
            OUTPUT_VARIABLE changes
            ERROR_QUIET
            RESULT_VARIABLE status_result)
        if(NOT status_result EQUAL 0 OR NOT changes STREQUAL "")
            string(APPEND build_version ".dirty")
        else()
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match
                    --match "v${PROJECT_VERSION}" HEAD
                WORKING_DIRECTORY "${SOURCE_DIR}"
                OUTPUT_VARIABLE release_tag
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE tag_result)
            if(tag_result EQUAL 0 AND
                release_tag STREQUAL "v${PROJECT_VERSION}")
                set(build_version "${PROJECT_VERSION}")
            endif()
        endif()
    endif()
endif()

set(content "#pragma once\n#define HIT_FMT_VERSION \"${build_version}\"\n")
if(EXISTS "${OUTPUT_FILE}")
    file(READ "${OUTPUT_FILE}" previous_content)
endif()
if(NOT content STREQUAL previous_content)
    file(WRITE "${OUTPUT_FILE}" "${content}")
endif()
