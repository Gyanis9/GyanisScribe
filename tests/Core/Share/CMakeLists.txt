add_executable(TestCoreShare
        TestShareRenderer.cpp
        TestShareStyle.cpp)

target_link_libraries(TestCoreShare PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestCoreShare PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCoreShare DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCoreShare)
