add_executable(TestUi
        TestDesignTokens.cpp
        TestIconProvider.cpp
        TestKeyStep.cpp
        TestLanguage.cpp
        TestThemeManager.cpp
        TestHotkeyEdit.cpp
        TestAppShortcuts.cpp
        TestToast.cpp)

target_link_libraries(TestUi PRIVATE
        Ui
        Core
        Settings
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestUi PRIVATE /W4 /WX)
endif ()

add_dependencies(TestUi DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestUi)
