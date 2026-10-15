add_executable(TestGuiAnnotation
        TestAnnotationToolbar.cpp
        TestAnnotationWindow.cpp)

target_link_libraries(TestGuiAnnotation PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestGuiAnnotation PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiAnnotation DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestGuiAnnotation)
