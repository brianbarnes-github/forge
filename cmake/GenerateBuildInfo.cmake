# Writes OUT_FILE (a C++ header) with the version, git commit count, short
# hash and dirty flag of the checkout at SOURCE_DIR. Run via `cmake -P` on
# every build (see the forge_build_info target), so the values track the
# commit being built rather than the one CMake was configured at.
# configure_file only rewrites the header when a value changed, so an
# unchanged commit triggers no recompilation.
#
# Inputs: SOURCE_DIR, OUT_FILE, TEMPLATE_FILE, PROJECT_VERSION.
# Without git (e.g. a source tarball) the count is 0 and the hash empty,
# which the About box shows as "Build unknown".

set(FORGE_BUILD_COMMIT_COUNT 0)
set(FORGE_BUILD_COMMIT_HASH "")
set(FORGE_BUILD_DIRTY false)

find_program(GIT_EXECUTABLE git)
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE count OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE countResult ERROR_QUIET)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE hash OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE hashResult ERROR_QUIET)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE status OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE statusResult ERROR_QUIET)

    if(countResult EQUAL 0 AND hashResult EQUAL 0)
        set(FORGE_BUILD_COMMIT_COUNT "${count}")
        set(FORGE_BUILD_COMMIT_HASH "${hash}")
        if(statusResult EQUAL 0 AND NOT status STREQUAL "")
            set(FORGE_BUILD_DIRTY true)
        endif()
    endif()
endif()

configure_file("${TEMPLATE_FILE}" "${OUT_FILE}" @ONLY)
