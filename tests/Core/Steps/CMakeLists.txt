add_executable(TestCoreSteps
        TestStepSession.cpp)

target_link_libraries(TestCoreSteps PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui)

if (MSVC)
    target_compile_options(TestCoreSteps PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCoreSteps DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCoreSteps)
