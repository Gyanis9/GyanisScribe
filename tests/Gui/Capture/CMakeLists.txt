find_package(Qt6 COMPONENTS Test REQUIRED)

add_executable(TestGuiCapture
        TestCaptureOverlay.cpp
        TestLongCaptureHud.cpp
        TestCaptureService.cpp)

target_link_libraries(TestGuiCapture PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Test)

if (MSVC)
    target_compile_options(TestGuiCapture PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiCapture DeployQtRuntime)

# QSignalSpy/QTest 在 Qt6Test 里，而共用产出目录的那份部署清单只摆产品要的几件；
# 少了这一份 DLL，用例可执行体起不来会被发现那一步报成「测试没跑起来」
add_custom_command(TARGET TestGuiCapture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:TestGuiCapture>/")

include(GoogleTest)
# 选区复制那几条借系统剪贴板传图：并行时读到的会是别的进程刚写进去的图（实测假红）。
# 锁名与 TestPin / TestOcr 的是同一把：跨二进制的互斥只在两边都拿它时成立。
gtest_discover_tests(TestGuiCapture PROPERTIES RESOURCE_LOCK clipboardShared)
