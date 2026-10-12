add_executable(TestCore
        TestLogger.cpp
        TestPaths.cpp
        TestImageFit.cpp
        TestImageFormat.cpp
        TestOutputDecoration.cpp
        TestClipboardImage.cpp)

target_link_libraries(TestCore PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestCore PRIVATE /W4 /WX)
endif ()

# 用例发现发生在构建期：先把 Qt 运行时副本摆好，否则缺 DLL 会被读成「测试没跑起来」
add_dependencies(TestCore DeployQtRuntime)

include(GoogleTest)
# 剪贴板用例（TestClipboardImage）真的往系统剪贴板里放东西：与别的借用剪贴板的套件同拿一把锁，
# 否则并行跑时读到的是别人写着半截的内容。分号要转义——裸分号会被 CMake 当列表分隔吃掉（旧账）
gtest_discover_tests(TestCore PROPERTIES RESOURCE_LOCK "clipboardShared")
