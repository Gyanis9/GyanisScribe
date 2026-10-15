add_executable(TestGuiControls
        TestKeyStep.cpp
        TestHotkeyEdit.cpp
        TestAppShortcuts.cpp
        TestToast.cpp)

target_link_libraries(TestGuiControls PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestGuiControls PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiControls DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestGuiControls)
