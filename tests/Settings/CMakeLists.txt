add_executable(TestSettings
        TestSettingsStore.cpp)

target_link_libraries(TestSettings PRIVATE
        Settings
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestSettings PRIVATE /W4 /WX)
endif ()

# 用例发现在构建期跑可执行体，先要有 Qt 运行时副本
add_dependencies(TestSettings DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestSettings)
