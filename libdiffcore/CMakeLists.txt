find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

set(DIFFCORE_PUBLIC_HEADERS
    include/diffcore/DiffTypes.h
    include/diffcore/LineInterner.h
    include/diffcore/DiffEngine.h
)

set(DIFFCORE_SOURCES
    src/LineInterner.cpp
    src/DiffEngine.cpp
)

set(DIFFCORE_INTERNAL_HEADERS
    src/internal/Diff.h
    src/internal/Structs.h
)

add_library(diffcore STATIC
    ${DIFFCORE_SOURCES}
    ${DIFFCORE_PUBLIC_HEADERS}
    ${DIFFCORE_INTERNAL_HEADERS}
)

# Namespaced alias so consumers can write diffcore::diffcore.
add_library(diffcore::diffcore ALIAS diffcore)

target_include_directories(diffcore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(diffcore PUBLIC Qt6::Core)

if(DIFFMERGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
