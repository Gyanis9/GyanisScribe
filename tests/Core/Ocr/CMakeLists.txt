add_executable(TestCoreOcr
        TestOcrPreprocessor.cpp
        TestRecognitionRunner.cpp)

target_link_libraries(TestCoreOcr PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestCoreOcr PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCoreOcr DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCoreOcr)
