add_executable(TestShell
        TestShell.cpp
        TestSingleInstance.cpp)

target_link_libraries(TestShell PRIVATE
        App
        Settings
        Ui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestShell PRIVATE /W4 /WX)
endif ()

add_dependencies(TestShell DeployQtRuntime)

# 主窗口的接线判据要真的建窗口，因此单独一个可执行体：QApplication 一个进程只能有一个，
# 与上面那两组不共用一份配置的用例隔开最省心
add_executable(TestMainWindow
        TestMainWindow.cpp)

target_link_libraries(TestMainWindow PRIVATE
        App
        Annotation
        History
        Pin
        Record
        Settings
        Ui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestMainWindow PRIVATE /W4 /WX)
endif ()

add_dependencies(TestMainWindow DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestShell)
gtest_discover_tests(TestMainWindow)
