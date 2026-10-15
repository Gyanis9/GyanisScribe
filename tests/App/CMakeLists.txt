add_executable(TestShell
        TestShell.cpp
        TestSingleInstance.cpp
        TestCrashGuard.cpp
        TestCaptureIntent.cpp
        TestCaptureDelivery.cpp
        TestCommandLine.cpp
        TestCustomHotkeys.cpp
        TestTrayClick.cpp
        TestImageFetcher.cpp
        TestUpdateCheck.cpp)

target_link_libraries(TestShell PRIVATE
        Main
        App
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestShell PRIVATE /W4 /WX)
endif ()

add_dependencies(TestShell DeployQtRuntime)

# 崩溃兜底只有真崩一次才验得出来，所以起一个测试专用的探针进程：它装好兜底然后读空指针。
# 「让程序崩」不能有产品入口，因此它不是 GyanisScribe 的一部分，也不进安装包。
add_executable(CrashProbe
        CrashProbe.cpp)

target_link_libraries(CrashProbe PRIVATE
        App
        Core
        Qt6::Core)

if (MSVC)
    target_compile_options(CrashProbe PRIVATE /W4 /WX)
endif ()

add_dependencies(CrashProbe DeployQtRuntime)

# 用例按这个宏起探针，路径由构建系统给，不写死本机位置
target_compile_definitions(TestShell PRIVATE GYANISSCRIBE_CRASH_PROBE="$<TARGET_FILE:CrashProbe>")

include(GoogleTest)
gtest_discover_tests(TestShell)
