add_executable(TestCorePick
        TestPick.cpp
        TestRecentColors.cpp)

target_link_libraries(TestCorePick PRIVATE
        Core
        Gui
        GTest::gtest
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestCorePick PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCorePick DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCorePick)
