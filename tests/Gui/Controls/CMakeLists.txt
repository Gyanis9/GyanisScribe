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

# 控件外观判据单独一个可执行体：TestGuiControls 里的 TestToast 已经持有一份 QApplication，
# 同一进程不能再造第二份。
add_executable(TestControlsChrome
        TestControlsChrome.cpp
        TestFocusRing.cpp
        TestStreamingText.cpp)

target_link_libraries(TestControlsChrome PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestControlsChrome PRIVATE /W4 /WX)
endif ()

add_dependencies(TestControlsChrome DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestControlsChrome)
