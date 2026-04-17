find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_cli test_cli.cpp)

target_link_libraries(test_cli PRIVATE Qt6::Test Qt6::Core)

# Pass paths to the CLI binary and fixtures as compile definitions so the
# test can locate them without hardcoding.
target_compile_definitions(test_cli PRIVATE
    DIFFMERGE_CLI_BINARY="$<TARGET_FILE:diffmerge>"
    DIFFMERGE_CLI_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME test_cli COMMAND test_cli)

# Make sure the CLI is built before the test runs.
add_dependencies(test_cli diffmerge)
