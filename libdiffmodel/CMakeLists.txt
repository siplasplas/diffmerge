# What comparisons and merges are, without showing them: prepared comparisons,
# merge sessions and exporting their results, for programs and services with
# no user interface. Only Qt Core and DiffMerge::Core.
find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

add_library(diffmerge-model STATIC
    src/AlignedLineModel.cpp
    src/Comparison.cpp
    src/ConflictMarkers.cpp
    src/IntraLineDiffEngine.cpp
    src/MergeExport.cpp
    src/MergeSession.cpp
    src/ScrollSyncMapper.cpp
    include/diffmerge/AlignedLineModel.h
    include/diffmerge/Comparison.h
    include/diffmerge/ConflictMarkers.h
    include/diffmerge/IntraLineDiffEngine.h
    include/diffmerge/MergeExport.h
    include/diffmerge/MergeSession.h
    include/diffmerge/ScrollSyncMapper.h
)
add_library(DiffMerge::Model ALIAS diffmerge-model)
set_target_properties(diffmerge-model PROPERTIES EXPORT_NAME Model POSITION_INDEPENDENT_CODE ON)
target_compile_features(diffmerge-model PUBLIC cxx_std_20)
target_include_directories(diffmerge-model PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(diffmerge-model PUBLIC DiffMerge::Core Qt6::Core)

install(TARGETS diffmerge-model EXPORT DiffMergeModelTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/diffmerge DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT DiffMergeModelTargets NAMESPACE DiffMerge::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DiffMerge)
