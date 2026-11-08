add_executable(TestCoreGeometry TestContinuousCorner.cpp)
target_link_libraries(TestCoreGeometry PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui)
if (MSVC)
    target_compile_options(TestCoreGeometry PRIVATE /W4 /WX)
endif ()
add_dependencies(TestCoreGeometry DeployQtRuntime)
include(GoogleTest)
gtest_discover_tests(TestCoreGeometry)
