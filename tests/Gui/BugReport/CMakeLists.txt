find_package(Qt6 COMPONENTS Test REQUIRED)

add_executable(TestGuiBugReport
        TestBugReportDialog.cpp)

target_link_libraries(TestGuiBugReport PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets
        Qt6::Gui
        Qt6::Test)

if (MSVC)
    target_compile_options(TestGuiBugReport PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiBugReport DeployQtRuntime)

# 剪贴板与别的套件共用一把锁：并行运行时两个进程往同一块系统剪贴板写会互相覆盖
add_custom_command(TARGET TestGuiBugReport POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:TestGuiBugReport>/")

include(GoogleTest)
gtest_discover_tests(TestGuiBugReport PROPERTIES RESOURCE_LOCK clipboardShared)
