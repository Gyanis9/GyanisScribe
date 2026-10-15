add_executable(TestGuiOcr
        TestOcrResultWindow.cpp)

target_link_libraries(TestGuiOcr PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestGuiOcr PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiOcr DeployQtRuntime)

include(GoogleTest)
# 识别结果复制那几条借系统剪贴板传文本：并行时读到的会是别的进程刚写进去的内容。
# 锁名与 TestPin / TestCapture 的是同一把：跨二进制的互斥只在两边都拿它时成立。
gtest_discover_tests(TestGuiOcr PROPERTIES RESOURCE_LOCK clipboardShared)
