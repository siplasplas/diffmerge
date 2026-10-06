cmake_minimum_required(VERSION 3.21)
project(DiffMerge VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for IDE tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt MOC/UIC/RCC - AUTOMOC is enough for libdiffcore (no .ui, no .qrc).
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(DIFFMERGE_BUILD_WIDGETS "Build embeddable Qt widgets" ON)
option(DIFFMERGE_BUILD_GUI        "Build GUI application"          ${PROJECT_IS_TOP_LEVEL})
option(DIFFMERGE_BUILD_CLI        "Build CLI application"          ${PROJECT_IS_TOP_LEVEL})
option(DIFFMERGE_BUILD_CORPUS_DL  "Build corpus downloader"        ${PROJECT_IS_TOP_LEVEL})
option(DIFFMERGE_BUILD_SLIDER_EVAL "Build slider evaluation tool"  ${PROJECT_IS_TOP_LEVEL})
option(DIFFMERGE_BUILD_TESTS      "Build tests"                    ${PROJECT_IS_TOP_LEVEL})

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

if(DIFFMERGE_BUILD_GUI AND NOT DIFFMERGE_BUILD_WIDGETS)
    message(FATAL_ERROR "DIFFMERGE_BUILD_GUI requires DIFFMERGE_BUILD_WIDGETS")
endif()
if(DIFFMERGE_BUILD_WIDGETS)
    add_subdirectory(diffmerge-gui)
endif()

if(DIFFMERGE_BUILD_CLI)
    add_subdirectory(diffmerge-cli)
endif()

if(DIFFMERGE_BUILD_CORPUS_DL)
    add_subdirectory(diffmerge-corpus-dl)
endif()

if(DIFFMERGE_BUILD_SLIDER_EVAL)
    add_subdirectory(diffmerge-slider-eval)
endif()

set(DIFFMERGE_PACKAGE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/DiffMerge")
configure_package_config_file(cmake/DiffMergeConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/DiffMergeConfig.cmake"
    INSTALL_DESTINATION "${DIFFMERGE_PACKAGE_DIR}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/DiffMergeConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/DiffMergeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/DiffMergeConfigVersion.cmake"
    DESTINATION "${DIFFMERGE_PACKAGE_DIR}")

# Ship the complete license terms and project policy with installed artifacts.
install(FILES LICENSE COPYING CONTRIBUTING.md
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/DiffMerge)
