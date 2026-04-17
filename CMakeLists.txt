cmake_minimum_required(VERSION 3.21)
project(DiffMerge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for IDE tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt MOC/UIC/RCC - AUTOMOC is enough for libdiffcore (no .ui, no .qrc).
set(CMAKE_AUTOMOC ON)

option(DIFFMERGE_BUILD_GUI   "Build GUI application"   ON)
option(DIFFMERGE_BUILD_CLI   "Build CLI application"   ON)
option(DIFFMERGE_BUILD_TESTS "Build tests"             ON)

# enable_testing() must be called in the top-level CMakeLists.txt for ctest
# to find tests registered in subdirectories.
if(DIFFMERGE_BUILD_TESTS)
    enable_testing()
endif()

# Treat warnings as warnings (not errors) on GCC/Clang; keep MSVC default.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(libdiffcore)

if(DIFFMERGE_BUILD_GUI)
    add_subdirectory(diffmerge-gui)
endif()

if(DIFFMERGE_BUILD_CLI)
    add_subdirectory(diffmerge-cli)
endif()
