find_package(Qt6 COMPONENTS Test REQUIRED)

add_executable(TestGuiRewind
        TestRewindPickerWindow.cpp)

target_link_libraries(TestGuiRewind PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets
        Qt6::Gui
        Qt6::Test)

if (MSVC)
    target_compile_options(TestGuiRewind PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiRewind DeployQtRuntime)

# 键盘驱动用例要用 QTest，而共用产出目录的部署清单只摆产品要的那几件；少这一份 DLL，
# 用例可执行体起不来会被发现那一步报成「测试没跑起来」
add_custom_command(TARGET TestGuiRewind POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:TestGuiRewind>/")

include(GoogleTest)
gtest_discover_tests(TestGuiRewind)
