add_executable(TestAnnotation
        TestAnnotationEngine.cpp
        TestAnnotationTools.cpp
        TestAnnotationToolbar.cpp
        TestAnnotationTransform.cpp
        TestAnnotationWindow.cpp
        TestBlankCanvas.cpp)

target_link_libraries(TestAnnotation PRIVATE
        Annotation
        Core
        Settings
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestAnnotation PRIVATE /W4 /WX)
endif ()

add_dependencies(TestAnnotation DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestAnnotation)
