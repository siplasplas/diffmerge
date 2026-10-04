find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Svg)
find_package(qcodeedit 1.6.0 CONFIG QUIET)
if(NOT qcodeedit_FOUND)
    include(FetchContent)

    # Only the editor library is needed by DiffMerge.
    set(QCE_BUILD_DEMO OFF)
    set(QCE_BUILD_TESTS OFF)
    set(QCE_BUILD_KATE OFF)
    set(QCE_BUILD_KATE_DOWNLOADER OFF)

    FetchContent_Declare(qcodeedit
        GIT_REPOSITORY https://github.com/siplasplas/qcodeedit.git
        GIT_TAG v1.6.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(qcodeedit)
    message(STATUS "DiffMerge: using fetched qcodeedit v1.6.0")
else()
    message(STATUS "DiffMerge: using installed qcodeedit ${qcodeedit_VERSION}")
endif()

set(DIFFMERGE_WIDGET_SOURCES
    src/common/ColorScheme.cpp
    src/editor/DiffEditor.cpp
    src/editor/DiffHighlighter.h
    src/editor/DiffHighlighter.cpp
    src/editor/IntraLineDiffEngine.cpp
    src/fileview/Comparison.cpp
    src/fileview/AlignedLineModel.cpp
    src/fileview/FileDiffWidget.cpp
    src/fileview/DiffConnectorSplitter.h
    src/fileview/DiffConnectorSplitter.cpp
    src/fileview/ScrollSyncMapper.cpp
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
    DiffMerge::Core Qt6::Widgets qcodeedit::qcodeedit PRIVATE Qt6::Svg)

if(DIFFMERGE_BUILD_GUI)
    include(FetchContent)
    set(QT_EXTRA_BUILD_DEMO OFF)
    set(QT_VERSION_MAJOR 6)
    FetchContent_Declare(qt_extra
        GIT_REPOSITORY https://github.com/siplasplas/qt-extra.git
        GIT_TAG v2.3.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(qt_extra)
    add_executable(diffmerge-gui src/main.cpp src/MainWindow.cpp src/MainWindow.h)
    target_link_libraries(diffmerge-gui PRIVATE DiffMerge::Widgets qt-extra)
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
