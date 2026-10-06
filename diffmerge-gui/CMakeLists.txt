find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Svg Concurrent)
find_package(qcodeedit 1.6.0 CONFIG QUIET)
if(qcodeedit_FOUND)
    find_package(qcodeedit-kate 1.6.0 CONFIG QUIET)
    if(DIFFMERGE_BUILD_GUI AND TARGET qcodeedit::kate)
        find_package(qcodeedit-katedata 1.6.0 CONFIG QUIET)
    endif()
endif()

if(NOT TARGET qcodeedit::qcodeedit OR NOT TARGET qcodeedit::kate OR
   (DIFFMERGE_BUILD_GUI AND NOT TARGET qcodeedit::katedata))
    include(FetchContent)
    set(QCE_BUILD_DEMO OFF)
    set(QCE_BUILD_TESTS OFF)
    set(QCE_BUILD_KATE ON)
    set(QCE_BUILD_KATE_DOWNLOADER ${DIFFMERGE_BUILD_GUI})

    # An installed core can coexist with companions built from the same release.
    if(TARGET qcodeedit::qcodeedit)
        set(_qce_source_subdir SOURCE_SUBDIR diffmerge-companions-only)
    else()
        set(_qce_source_subdir)
    endif()
    FetchContent_Declare(qcodeedit
        GIT_REPOSITORY https://github.com/siplasplas/qcodeedit.git
        GIT_TAG v1.6.0
        GIT_SHALLOW TRUE
        ${_qce_source_subdir}
    )
    FetchContent_MakeAvailable(qcodeedit)
    if(NOT TARGET qcodeedit::kate OR (DIFFMERGE_BUILD_GUI AND NOT TARGET qcodeedit::katedata))
        # The companion install templates are relative to the upstream project.
        function(diffmerge_add_qce_companions)
            set(PROJECT_SOURCE_DIR "${qcodeedit_SOURCE_DIR}")
            set(PROJECT_VERSION 1.6.0)
            if(NOT TARGET qcodeedit::kate)
                add_subdirectory("${qcodeedit_SOURCE_DIR}/kate" "${qcodeedit_BINARY_DIR}/kate")
            endif()
            if(DIFFMERGE_BUILD_GUI AND NOT TARGET qcodeedit::katedata)
                find_package(Qt6 6.2 REQUIRED COMPONENTS Network)
                add_subdirectory("${qcodeedit_SOURCE_DIR}/katedata" "${qcodeedit_BINARY_DIR}/katedata")
            endif()
        endfunction()
        diffmerge_add_qce_companions()
    endif()
    message(STATUS "DiffMerge: using fetched qcodeedit components v1.6.0")
else()
    message(STATUS "DiffMerge: using installed qcodeedit ${qcodeedit_VERSION} with Kate support")
endif()

set(DIFFMERGE_WIDGET_SOURCES
    src/common/ColorScheme.cpp
    src/editor/DiffEditor.cpp
    src/editor/DiffHighlighter.h
    src/editor/DiffHighlighter.cpp
    src/editor/SyntaxLoader.h
    src/editor/SyntaxLoader.cpp
    src/editor/IntraLineDiffEngine.cpp
    src/fileview/ViewProjection.cpp
    src/merge/ConflictMarkers.cpp
    src/merge/MergeExport.cpp
    include/diffmerge/MergeExport.h
    src/merge/MergeWidget.cpp
    include/diffmerge/MergeWidget.h
    src/merge/MergePreviewWidget.cpp
    include/diffmerge/ConflictMarkers.h
    include/diffmerge/MergePreviewWidget.h
    src/merge/MergeSession.cpp
    include/diffmerge/MergeSession.h
    src/fileview/Comparison.cpp
    src/fileview/AlignedLineModel.cpp
    src/fileview/FileDiffWidget.cpp
    src/fileview/DiffConnectorSplitter.h
    src/fileview/DiffConnectorSplitter.cpp
    src/fileview/ScrollSyncMapper.cpp
    src/fileview/FileEditing.cpp
    src/fileview/FileReload.cpp
    src/fileview/ByteComparison.cpp
    src/dirview/DirectoryOperations.cpp
    src/dirview/DirDiffModel.cpp
    src/dirview/DirDiffWidget.cpp
    include/diffmerge/Comparison.h
    include/diffmerge/ColorScheme.h
    include/diffmerge/DiffEditor.h
    include/diffmerge/IntraLineDiffEngine.h
    include/diffmerge/AlignedLineModel.h
    include/diffmerge/FileDiffWidget.h
    include/diffmerge/ScrollSyncMapper.h
    include/diffmerge/DirDiffModel.h
    include/diffmerge/DirDiffWidget.h
)

# Named resource and explicit initialization keep icons available in static consumers.
qt_add_resources(DIFFMERGE_WIDGET_SOURCES res/diffmerge_widgets.qrc)
add_library(diffmerge-widgets STATIC ${DIFFMERGE_WIDGET_SOURCES})
add_library(DiffMerge::Widgets ALIAS diffmerge-widgets)
set_target_properties(diffmerge-widgets PROPERTIES EXPORT_NAME Widgets POSITION_INDEPENDENT_CODE ON)
target_compile_features(diffmerge-widgets PUBLIC cxx_std_20)
target_include_directories(diffmerge-widgets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(diffmerge-widgets PUBLIC
    DiffMerge::Core Qt6::Widgets qcodeedit::qcodeedit PRIVATE Qt6::Svg Qt6::Concurrent qcodeedit::kate)

if(DIFFMERGE_BUILD_GUI)
    include(FetchContent)
    set(QT_EXTRA_BUILD_DEMO OFF)
    set(QT_VERSION_MAJOR 6)
    FetchContent_Declare(qt_extra
        GIT_REPOSITORY https://github.com/siplasplas/qt-extra.git
        GIT_TAG v2.3.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(qt_extra)
    add_executable(diffmerge-gui src/main.cpp src/LaunchOptions.cpp src/LaunchOptions.h src/MainWindow.cpp src/MainWindow.h src/MergeResultSave.cpp src/MergeResultSave.h src/MergeDialog.cpp src/MergeDialog.h)
    target_link_libraries(diffmerge-gui PRIVATE DiffMerge::Widgets qt-extra qcodeedit::katedata)
    install(TARGETS diffmerge-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS diffmerge-widgets EXPORT DiffMergeWidgetsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/diffmerge DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT DiffMergeWidgetsTargets NAMESPACE DiffMerge::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DiffMerge)

if(DIFFMERGE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
