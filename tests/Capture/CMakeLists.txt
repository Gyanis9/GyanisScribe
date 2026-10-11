add_executable(TestCapture
        TestCaptureService.cpp
        TestCursorPixels.cpp
        TestFrameStitch.cpp
        TestLongCaptureHud.cpp
        TestCaptureOverlay.cpp
        TestQScreenCaptureSource.cpp
        TestScreenUtils.cpp)

target_link_libraries(TestCapture PRIVATE
        Capture
        Settings
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestCapture PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCapture DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCapture)
