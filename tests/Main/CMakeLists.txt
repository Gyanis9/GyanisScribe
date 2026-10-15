# 主窗口的接线判据要真的建窗口，因此单独一个可执行体：QApplication 一个进程只能有一个，
# 与 tests/App 那两组不共用一份配置的用例隔开最省心
add_executable(TestMainWindow
        TestClipboardPin.cpp
        TestMainWindow.cpp)

# 两帧 GIF 那条小素材住在 tests/Core/Pin/PinTestAssets.h：拖放里的动图分支与 Pin 组共用它，
# 不在两处各打一份字节（两份会随着将来改素材而分家）
target_include_directories(TestMainWindow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Core/Pin)

target_link_libraries(TestMainWindow PRIVATE
        Main
        App
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestMainWindow PRIVATE /W4 /WX)
endif ()

add_dependencies(TestMainWindow DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestMainWindow)

# 跨模块流程的集成入口（需求 §10「提供集成测试入口，覆盖截图、贴图、OCR 流程」）：
# 一条链上同时用抓帧服务、标注引擎、贴图窗口与识别后端，而且要自己的一份 QApplication，
# 因此与上面几组各占一个可执行体（QApplication 一个进程只能有一个）
add_executable(TestModuleFlow
        TestModuleFlow.cpp)

target_link_libraries(TestModuleFlow PRIVATE
        Main
        App
        Core
        Platform
        Gui
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestModuleFlow PRIVATE /W4 /WX)
endif ()

add_dependencies(TestModuleFlow DeployQtRuntime)

# 布局那两条用例写的是真实缓存目录（cacheDirectory()/Pins），目录名由 Core 的贴图布局自己定，
# 测试改不了；ctest 把每条用例当独立进程并行起，因此这一组要按资源锁串行，
# 否则一个进程删掉的文件是另一个进程正要读的。拖放那一条借系统剪贴板读回像素，
# 与 TestGuiPin / TestGuiCapture / TestGuiOcr 同拿 clipboardShared 那一把。
gtest_discover_tests(TestModuleFlow PROPERTIES RESOURCE_LOCK "pinLayoutCache\;clipboardShared")

# AppController 那一层的直测：延时截图的路由与成品进历史要判「谁在什么时刻被叫到」，
# 因此给它一套与 TestModuleFlow 同形状的替身（AppControllerParts 注进去，见 TestAppController.cpp）。
# 控制器要建真窗口，自己一份 QApplication，与上面几组各占一个可执行体。
add_executable(TestAppController
        TestAppController.cpp)

target_link_libraries(TestAppController PRIVATE
        Main
        App
        Core
        Platform
        Gui
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestAppController PRIVATE /W4 /WX)
endif ()

add_dependencies(TestAppController DeployQtRuntime)

# 这一组会碰贴图布局缓存、未保存归档目录与系统剪贴板（快速保存那一条会写剪贴板）：
# 与 TestModuleFlow 同拿那两把锁——并行跑时一组清的正是另一组在读的。
gtest_discover_tests(TestAppController PROPERTIES RESOURCE_LOCK "pinLayoutCache\;clipboardShared")

# 文档出图工具：离屏起一份真主窗口逐页拍 PNG（README「界面一览」用）。
# 与 CrashProbe 同一条理由不是产品的一部分：不进安装包、不挂产品入口，只给文档出图。
add_executable(UiShotTool
        UiShotTool.cpp)

target_link_libraries(UiShotTool PRIVATE
        Main
        App
        Platform
        Gui
        Core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(UiShotTool PRIVATE /W4 /WX)
endif ()

add_dependencies(UiShotTool DeployQtRuntime)

# 界面审查出图工具：离屏起真部件逐面拍 PNG 并把尺寸读数打进 stdout（界面审查台账的取证，台账在仓外 GyanisScribe-Reports/）。
# 与 UiShotTool 并列而不是合并：那一份服务 README 的文档图，这一份服务审查台账；理由见文件头。
add_executable(UiAuditTool
        UiAuditTool.cpp)

target_link_libraries(UiAuditTool PRIVATE
        Main
        App
        Platform
        Gui
        Core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(UiAuditTool PRIVATE /W4 /WX)
endif ()

add_dependencies(UiAuditTool DeployQtRuntime)
