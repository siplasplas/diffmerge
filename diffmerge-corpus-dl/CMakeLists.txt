find_package(Qt6 REQUIRED COMPONENTS Core Network)

set(CORPUS_DL_SOURCES
    src/main.cpp
    src/SliderParser.h
    src/SliderParser.cpp
    src/Downloader.h
    src/Downloader.cpp
    src/CorpusProcessor.h
    src/CorpusProcessor.cpp
)

add_executable(corpus-dl ${CORPUS_DL_SOURCES})
target_include_directories(corpus-dl PRIVATE src)
target_link_libraries(corpus-dl PRIVATE Qt6::Core Qt6::Network)

add_executable(corpus-annotate src/annotate_main.cpp)
target_link_libraries(corpus-annotate PRIVATE Qt6::Core)

if(DIFFMERGE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
