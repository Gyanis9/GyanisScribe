add_executable(TestCoreRewind
        TestRewindConfig.cpp)

target_link_libraries(TestCoreRewind PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui)

if (MSVC)
    target_compile_options(TestCoreRewind PRIVATE /W4 /WX)
endif ()

# 用例发现在构建期跑可执行体，先要有 Qt 运行时副本
add_dependencies(TestCoreRewind DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCoreRewind)
