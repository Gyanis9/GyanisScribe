add_executable(TestBase
        TestLogger.cpp
        TestPaths.cpp
        TestExternalCommand.cpp)

target_link_libraries(TestBase PRIVATE
        Base
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestBase PRIVATE /W4 /WX)
endif ()

# 用例发现发生在构建期：先把 Qt 运行时副本摆好，否则缺 DLL 会被读成「测试没跑起来」
add_dependencies(TestBase DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestBase)

# 外部命令的「真起一个进程」那两条用例拿这一份测试可执行体自己当探针：
# 带一个匹配不到任何用例的过滤器，起得来、立刻退，不会递归再跑一遍套件。路径由构建系统给，不写死本机位置
target_compile_definitions(TestBase PRIVATE GYANISSCRIBE_EXTERNAL_COMMAND_PROBE="$<TARGET_FILE:TestBase>")
