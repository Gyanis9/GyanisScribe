add_executable(TestGuiTheme
        TestDesignTokens.cpp
        TestLanguage.cpp
        TestMotion.cpp
        TestThemeManager.cpp)

target_link_libraries(TestGuiTheme PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

# 自绘读数没有 QLabel 可断言字体：源码级接线用例按这个根目录读 CaptureOverlay/ColorPickerOverlay/PinWindow
target_compile_definitions(TestGuiTheme PRIVATE GYANISSCRIBE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if (MSVC)
    target_compile_options(TestGuiTheme PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiTheme DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestGuiTheme)
