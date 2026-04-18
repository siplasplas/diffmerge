find_package(Qt6 REQUIRED COMPONENTS Core Test)

add_executable(test_slider_parser
    test_slider_parser.cpp
    ../src/SliderParser.cpp
)
target_include_directories(test_slider_parser PRIVATE ../src)
target_link_libraries(test_slider_parser PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_slider_parser COMMAND test_slider_parser)
