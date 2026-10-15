add_executable(TestGuiTheme
        TestDesignTokens.cpp
        TestLanguage.cpp
        TestThemeManager.cpp)

target_link_libraries(TestGuiTheme PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestGuiTheme PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiTheme DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestGuiTheme)
