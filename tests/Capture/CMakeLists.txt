add_executable(TestCapture
        TestBgraFrame.cpp
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
# 选区复制那几条借系统剪贴板传图：并行时读到的会是别的进程刚写进去的图。
# 锁名与 TestPin / TestOcr 的是同一把：跨二进制的互斥只在两边都拿它时成立。
gtest_discover_tests(TestCapture PROPERTIES RESOURCE_LOCK clipboardShared)
