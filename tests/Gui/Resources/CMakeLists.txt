add_executable(TestGuiResources
        TestIconProvider.cpp)

target_link_libraries(TestGuiResources PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestGuiResources PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiResources DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestGuiResources)
