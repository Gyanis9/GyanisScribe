add_executable(TestOcr
        TestOcrEngines.cpp
        TestOcrPreprocessor.cpp
        TestOcrResultWindow.cpp)

target_link_libraries(TestOcr PRIVATE
        Ocr
        Settings
        Platform
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestOcr PRIVATE /W4 /WX)
endif ()

add_dependencies(TestOcr DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestOcr)
