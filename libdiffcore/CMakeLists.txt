find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

set(DIFFCORE_PUBLIC_HEADERS
    include/diffcore/ComputationControl.h
    include/diffcore/DiffTypes.h
    include/diffcore/LineInterner.h
    include/diffcore/DiffEngine.h
    include/diffcore/SequenceDiff.h
    include/diffcore/SliderHeuristics.h
)

set(DIFFCORE_SOURCES
    src/LineInterner.cpp
    src/DiffEngine.cpp
    src/SequenceDiff.cpp
    src/SliderHeuristics.cpp
)

set(DIFFCORE_INTERNAL_HEADERS
    include/diffcore/detail/Diff.h
    include/diffcore/detail/Structs.h
)

add_library(diffcore STATIC
    ${DIFFCORE_SOURCES}
    ${DIFFCORE_PUBLIC_HEADERS}
    ${DIFFCORE_INTERNAL_HEADERS}
)

# Namespaced alias so consumers can write diffcore::diffcore.
add_library(diffcore::diffcore ALIAS diffcore)
add_library(DiffMerge::Core ALIAS diffcore)
set_target_properties(diffcore PROPERTIES EXPORT_NAME Core POSITION_INDEPENDENT_CODE ON)
target_compile_features(diffcore PUBLIC cxx_std_20)

target_include_directories(diffcore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(diffcore PUBLIC Qt6::Core)

if(DIFFMERGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS diffcore EXPORT DiffMergeCoreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/diffcore DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT DiffMergeCoreTargets NAMESPACE DiffMerge::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DiffMerge)
