add_executable(TestCoreBugReport
        TestBugReport.cpp)

target_link_libraries(TestCoreBugReport PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestCoreBugReport PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCoreBugReport DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCoreBugReport)
