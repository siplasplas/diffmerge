find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
find_package(qcodeedit 1.1 REQUIRED)

# AlignedLineModel is a pure logic class; test it in isolation by
# compiling the two source files we need directly into the test binary.
add_executable(test_aligned_model
    test_aligned_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/fileview/AlignedLineModel.cpp
)

target_include_directories(test_aligned_model PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(test_aligned_model
    PRIVATE
        diffcore::diffcore
        Qt6::Core
        Qt6::Test
)

add_test(NAME test_aligned_model COMMAND test_aligned_model)

add_executable(test_scroll_sync_mapper
    test_scroll_sync_mapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/fileview/ScrollSyncMapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/fileview/AlignedLineModel.cpp
)
target_include_directories(test_scroll_sync_mapper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(test_scroll_sync_mapper
    PRIVATE
        diffcore::diffcore
        Qt6::Core
        Qt6::Test
)
add_test(NAME test_scroll_sync_mapper COMMAND test_scroll_sync_mapper)

# screenshot_tool is a developer helper, not a test - it renders the
# FileDiffWidget offscreen and saves a PNG.
set(GUI_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_executable(screenshot_tool
    screenshot_tool.cpp
    ${GUI_SRC_DIR}/common/ColorScheme.cpp
    ${GUI_SRC_DIR}/editor/DiffEditor.cpp
    ${GUI_SRC_DIR}/fileview/AlignedLineModel.cpp
    ${GUI_SRC_DIR}/fileview/FileDiffWidget.cpp
    ${GUI_SRC_DIR}/fileview/ScrollSyncMapper.cpp
)
target_include_directories(screenshot_tool PRIVATE ${GUI_SRC_DIR})
target_link_libraries(screenshot_tool
    PRIVATE
        diffcore::diffcore
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        qcodeedit::qcodeedit
)
