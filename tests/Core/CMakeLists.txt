add_executable(TestCore
        TestLogger.cpp
        TestPaths.cpp
        TestImageFit.cpp
        TestImageFormat.cpp)

target_link_libraries(TestCore PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestCore PRIVATE /W4 /WX)
endif ()

# 用例发现发生在构建期：先把 Qt 运行时副本摆好，否则缺 DLL 会被读成「测试没跑起来」
add_dependencies(TestCore DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCore)
