add_executable(TestCoreSettings
        TestNamingRules.cpp
        TestOutputStyleRules.cpp
        TestSettingsStore.cpp)

target_link_libraries(TestCoreSettings PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestCoreSettings PRIVATE /W4 /WX)
endif ()

# 用例发现在构建期跑可执行体，先要有 Qt 运行时副本
add_dependencies(TestCoreSettings DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCoreSettings)
