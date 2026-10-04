find_package(Qt6 REQUIRED COMPONENTS Test Widgets)

foreach(test IN ITEMS test_diff_editor test_aligned_model test_scroll_sync_mapper test_intra_line_diff)
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE DiffMerge::Widgets Qt6::Test)
    add_test(NAME ${test} COMMAND ${test})
endforeach()
# Connector geometry is an internal implementation detail tested here.
set_tests_properties(test_diff_editor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(screenshot_tool screenshot_tool.cpp)
target_link_libraries(screenshot_tool PRIVATE DiffMerge::Widgets)
