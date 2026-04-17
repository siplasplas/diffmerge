find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

add_executable(diffmerge
    src/main.cpp
    src/CliOptions.h
    src/CliOptions.cpp
    src/FileLoader.h
    src/FileLoader.cpp
    src/HunkPrinter.h
    src/HunkPrinter.cpp
)

target_link_libraries(diffmerge
    PRIVATE
        diffcore::diffcore
        Qt6::Core
)

# Tests directory: integration tests comparing real files on disk.
if(DIFFMERGE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
