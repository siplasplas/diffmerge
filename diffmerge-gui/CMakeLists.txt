find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

set(DIFFMERGE_GUI_SOURCES
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp

    src/common/ColorScheme.h
    src/common/ColorScheme.cpp

    src/editor/DiffEditor.h
    src/editor/DiffEditor.cpp
    src/editor/DiffGutter.h
    src/editor/DiffGutter.cpp
    src/editor/DiffSideMargin.h
    src/editor/DiffSideMargin.cpp

    src/fileview/AlignedLineModel.h
    src/fileview/AlignedLineModel.cpp
    src/fileview/FileDiffWidget.h
    src/fileview/FileDiffWidget.cpp
)

add_executable(diffmerge-gui ${DIFFMERGE_GUI_SOURCES})

target_include_directories(diffmerge-gui PRIVATE src)

target_link_libraries(diffmerge-gui
    PRIVATE
        diffcore::diffcore
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

if(DIFFMERGE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
