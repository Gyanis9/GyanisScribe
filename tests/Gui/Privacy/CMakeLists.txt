find_package(Qt6 COMPONENTS Test REQUIRED)

add_executable(TestGuiPrivacy
        TestPrivacyReviewDialog.cpp)

target_link_libraries(TestGuiPrivacy PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets
        Qt6::Gui
        Qt6::Test)

if (MSVC)
    target_compile_options(TestGuiPrivacy PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiPrivacy DeployQtRuntime)

# 对话框用例要 QApplication 与 Qt 测试运行库，而共用产出目录的部署清单只摆产品要的那几件；
# 少这一份 DLL，用例可执行体起不来会被发现那一步报成「测试没跑起来」
add_custom_command(TARGET TestGuiPrivacy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:TestGuiPrivacy>/")

include(GoogleTest)
gtest_discover_tests(TestGuiPrivacy)
