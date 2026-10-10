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

# 跨模块流程的集成入口（需求 §10「提供集成测试入口，覆盖截图、贴图、OCR 流程」）：
# 一条链上同时用抓帧服务、标注引擎、贴图窗口与识别后端，而且要自己的一份 QApplication，
# 因此与上面两组各占一个可执行体（QApplication 一个进程只能有一个）
add_executable(TestModuleFlow
        TestModuleFlow.cpp)

target_link_libraries(TestModuleFlow PRIVATE
        Annotation
        Capture
        Core
        Ocr
        Pin
        Platform
        Settings
        Ui
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestModuleFlow PRIVATE /W4 /WX)
endif ()

add_dependencies(TestModuleFlow DeployQtRuntime)

# 布局那两条用例写的是真实缓存目录（cacheDirectory()/Pins），目录名由 Pin 模块自己定，
# 测试改不了；ctest 把每条用例当独立进程并行起，因此这一组要按资源锁串行，
# 否则一个进程删掉的文件是另一个进程正要读的
gtest_discover_tests(TestModuleFlow PROPERTIES RESOURCE_LOCK pinLayoutCache)
