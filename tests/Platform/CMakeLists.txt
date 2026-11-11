add_executable(TestPlatform
        TestPlatform.cpp
        TestWindowGeometry.cpp
        TestSessionNotification.cpp
        TestStepEventSource.cpp
        TestCrashGuard.cpp)

find_package(Qt6 COMPONENTS Test REQUIRED)

target_link_libraries(TestPlatform PRIVATE
        Platform
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets
        Qt6::Test)

# QSignalSpy 在 Qt6Test 里，而共用产出目录的那份部署清单只摆产品要的几件；
# 少了这一份 DLL，用例可执行体起不来会被发现那一步报成「测试没跑起来」
add_custom_command(TARGET TestPlatform POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:TestPlatform>/")

if (MSVC)
    target_compile_options(TestPlatform PRIVATE /W4 /WX)
endif ()

# 端到端那条用例要真的建一扇窗口，靠的是可执行体旁边那套 Qt 运行时无界面插件；
# 副本跟着产出层走，所以挂在部署目标上而不是自己的 POST_BUILD
add_dependencies(TestPlatform DeployQtRuntime)

# 崩溃兜底只有真崩一次才验得出来，所以起一个测试专用的探针进程：它装好兜底然后读空指针。
# 「让程序崩」不能有产品入口，因此它不是 GyanisScribe 的一部分，也不进安装包。
add_executable(CrashProbe
        CrashProbe.cpp)

target_link_libraries(CrashProbe PRIVATE
        Platform
        Qt6::Core)

if (MSVC)
    target_compile_options(CrashProbe PRIVATE /W4 /WX)
endif ()

add_dependencies(CrashProbe DeployQtRuntime)

# 用例按这个宏起探针，路径由构建系统给，不写死本机位置
target_compile_definitions(TestPlatform PRIVATE GYANISSCRIBE_CRASH_PROBE="$<TARGET_FILE:CrashProbe>")

include(GoogleTest)
gtest_discover_tests(TestPlatform)
