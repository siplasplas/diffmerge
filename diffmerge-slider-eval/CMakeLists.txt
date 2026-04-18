find_package(Qt6 REQUIRED COMPONENTS Core)

add_executable(slider-eval
    src/main.cpp
    src/SliderEval.h
    src/SliderEval.cpp
)
target_include_directories(slider-eval PRIVATE src)
target_link_libraries(slider-eval PRIVATE diffcore::diffcore Qt6::Core)

if(DIFFMERGE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
