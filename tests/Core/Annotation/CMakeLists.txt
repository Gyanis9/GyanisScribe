add_executable(TestCoreAnnotation
        TestAnnotationEngine.cpp
        TestAnnotationTools.cpp
        TestAnnotationPalette.cpp
        TestAnnotationTransform.cpp
        TestBlankCanvas.cpp)

target_link_libraries(TestCoreAnnotation PRIVATE
        Core
        Gui
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestCoreAnnotation PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCoreAnnotation DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCoreAnnotation)
