add_executable(TestHistory
        TestHistory.cpp
        TestUnsavedArchive.cpp
        TestHistoryView.cpp)

# 视图与图标那一半要 Qt 的 GUI 栈，所以这里连 Widgets 与 Ui
target_link_libraries(TestHistory PRIVATE
        History
        Settings
        Core
        Ui
        GTest::gtest
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestHistory PRIVATE /W4 /WX)
endif ()

add_dependencies(TestHistory DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestHistory)
