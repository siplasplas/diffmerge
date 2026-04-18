find_package(Qt6 REQUIRED COMPONENTS Core Test)

add_executable(test_slider_eval
    test_slider_eval.cpp
    ../src/SliderEval.cpp
)
target_include_directories(test_slider_eval PRIVATE ../src)
target_link_libraries(test_slider_eval PRIVATE diffcore::diffcore Qt6::Core Qt6::Test)
add_test(NAME test_slider_eval COMMAND test_slider_eval)
