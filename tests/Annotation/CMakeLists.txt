add_executable(TestAnnotation
        TestAnnotationEngine.cpp
        TestAnnotationTools.cpp)

target_link_libraries(TestAnnotation PRIVATE
        Annotation
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestAnnotation PRIVATE /W4 /WX)
endif ()

add_dependencies(TestAnnotation DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestAnnotation)
