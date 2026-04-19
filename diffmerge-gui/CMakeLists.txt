find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Svg)
find_package(qcodeedit 1.1 REQUIRED)

set(DIFFMERGE_GUI_SOURCES
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp

    src/common/ColorScheme.h
    src/common/ColorScheme.cpp

    src/editor/DiffEditor.h
    src/editor/DiffEditor.cpp
    src/editor/DiffHighlighter.h
    src/editor/DiffHighlighter.cpp
    src/editor/IntraLineDiffEngine.h
    src/editor/IntraLineDiffEngine.cpp

    src/fileview/AlignedLineModel.h
    src/fileview/AlignedLineModel.cpp
    src/fileview/FileDiffWidget.h
    src/fileview/FileDiffWidget.cpp
    src/fileview/ScrollSyncMapper.h
    src/fileview/ScrollSyncMapper.cpp

    src/dirview/DirDiffModel.h
    src/dirview/DirDiffModel.cpp
    src/dirview/DirDiffWidget.h
    src/dirview/DirDiffWidget.cpp
)

qt_add_resources(DIFFMERGE_GUI_SOURCES res/resources.qrc)

add_executable(diffmerge-gui ${DIFFMERGE_GUI_SOURCES})

target_include_directories(diffmerge-gui PRIVATE src)

target_link_libraries(diffmerge-gui
    PRIVATE
        diffcore::diffcore
        Qt6::Core
        Qt6::Gui
        Qt6::Svg
        Qt6::Widgets
        qcodeedit::qcodeedit
)

if(DIFFMERGE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
