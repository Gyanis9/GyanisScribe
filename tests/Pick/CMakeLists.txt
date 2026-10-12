add_executable(TestPick
        TestPick.cpp
        TestRecentColors.cpp)

target_link_libraries(TestPick PRIVATE
        Pick
        Settings
        Core
        Ui
        GTest::gtest
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestPick PRIVATE /W4 /WX)
endif ()

add_dependencies(TestPick DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestPick)
