add_executable(TestGuiCapture
        TestCaptureOverlay.cpp
        TestLongCaptureHud.cpp
        TestCaptureService.cpp)

target_link_libraries(TestGuiCapture PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestGuiCapture PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiCapture DeployQtRuntime)

include(GoogleTest)
# 选区复制那几条借系统剪贴板传图：并行时读到的会是别的进程刚写进去的图（实测假红）。
# 锁名与 TestPin / TestOcr 的是同一把：跨二进制的互斥只在两边都拿它时成立。
gtest_discover_tests(TestGuiCapture PROPERTIES RESOURCE_LOCK clipboardShared)
