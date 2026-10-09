add_executable(TestHotkey
        TestHotkeyCombination.cpp
        TestHotkeyManager.cpp)

target_link_libraries(TestHotkey PRIVATE
        Hotkey
        Settings
        Platform
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestHotkey PRIVATE /W4 /WX)
endif ()

add_dependencies(TestHotkey DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestHotkey)
