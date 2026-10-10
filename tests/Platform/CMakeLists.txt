add_executable(TestPlatform
        TestPlatform.cpp
        TestWindowGeometry.cpp)

target_link_libraries(TestPlatform PRIVATE
        Platform
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestPlatform PRIVATE /W4 /WX)
endif ()

# 端到端那条用例要真的建一扇窗口，靠的是可执行体旁边那套 Qt 运行时无界面插件；
# 副本跟着产出层走，所以挂在部署目标上而不是自己的 POST_BUILD
add_dependencies(TestPlatform DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestPlatform)
