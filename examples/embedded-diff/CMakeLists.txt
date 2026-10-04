cmake_minimum_required(VERSION 3.21)
project(EmbeddedDiffExample LANGUAGES CXX)

# Set this to the repository root to exercise add_subdirectory instead of installation.
set(DIFFMERGE_SOURCE_DIR "" CACHE PATH "Optional DiffMerge source checkout")
if(DIFFMERGE_SOURCE_DIR)
    add_subdirectory("${DIFFMERGE_SOURCE_DIR}" diffmerge)
else()
    find_package(DiffMerge 1.0 CONFIG REQUIRED COMPONENTS Widgets)
endif()

find_package(Qt6 REQUIRED COMPONENTS Concurrent)
add_executable(embedded-diff main.cpp)
target_link_libraries(embedded-diff PRIVATE DiffMerge::Widgets Qt6::Concurrent)
