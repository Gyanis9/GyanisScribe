add_executable(TestCoreHotkey
        TestHotkeyCombination.cpp
        TestHotkeyManager.cpp)

target_link_libraries(TestCoreHotkey PRIVATE
        Core
        Platform
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestCoreHotkey PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCoreHotkey DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCoreHotkey)
