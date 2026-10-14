add_executable(TestShell
        TestShell.cpp
        TestSingleInstance.cpp
        TestCrashGuard.cpp
        TestCaptureIntent.cpp
        TestCaptureDelivery.cpp
        TestCommandLine.cpp
        TestUpdateCheck.cpp)

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

# 崩溃兜底只有真崩一次才验得出来，所以起一个测试专用的探针进程：它装好兜底然后读空指针。
# 「让程序崩」不能有产品入口，因此它不是 GyanisScribe 的一部分，也不进安装包。
add_executable(CrashProbe
        CrashProbe.cpp)

target_link_libraries(CrashProbe PRIVATE
        App
        Core
        Qt6::Core)

if (MSVC)
    target_compile_options(CrashProbe PRIVATE /W4 /WX)
endif ()

add_dependencies(CrashProbe DeployQtRuntime)

# 用例按这个宏起探针，路径由构建系统给，不写死本机位置
target_compile_definitions(TestShell PRIVATE GYANISSCRIBE_CRASH_PROBE="$<TARGET_FILE:CrashProbe>")

# 主窗口的接线判据要真的建窗口，因此单独一个可执行体：QApplication 一个进程只能有一个，
# 与上面那两组不共用一份配置的用例隔开最省心
add_executable(TestMainWindow
        TestClipboardPin.cpp
        TestMainWindow.cpp)

# 两帧 GIF 那条小素材住在 tests/Pin/PinTestAssets.h：拖放里的动图分支与 Pin 组共用它，
# 不在两处各打一份字节（两份会随着将来改素材而分家）
target_include_directories(TestMainWindow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Pin)

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
        App
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
# 否则一个进程删掉的文件是另一个进程正要读的。拖放那一条借系统剪贴板读回像素，
# 与 TestPin / TestCapture / TestOcr 同拿 clipboardShared 那一把。
gtest_discover_tests(TestModuleFlow PROPERTIES RESOURCE_LOCK "pinLayoutCache\;clipboardShared")
