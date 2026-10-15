add_executable(TestCoreCapture
        TestBgraFrame.cpp
        TestCursorPixels.cpp
        TestFrameStitch.cpp
        TestRegionHistory.cpp
        TestScreenUtils.cpp)

target_link_libraries(TestCoreCapture PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestCoreCapture PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCoreCapture DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCoreCapture)
