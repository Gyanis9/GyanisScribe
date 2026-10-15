add_executable(TestOcr
        TestOcrEngines.cpp
        TestOcrPreprocessor.cpp
        TestRecognitionRunner.cpp
        TestOcrResultWindow.cpp)

# 系统识别后端的直测跟着库那一档的条件走（同一台机器、同一个编译器才有这份后端）；
# 容器那一档这个文件不进编译列表，也不会有没被编过的用例文件
if (WIN32 AND MSVC)
    target_sources(TestOcr PRIVATE TestWinRTImageOcrEngine.cpp)
endif ()

# Tesseract 那一档同理，判据直接问库导出的宏，不在这里重复一遍 find_package：
# 本机 Debug 档没有这份后端（见 conanfile.py 的说明），容器两档都有
get_target_property(ocrInterfaceCompileDefinitions Ocr INTERFACE_COMPILE_DEFINITIONS)
if (ocrInterfaceCompileDefinitions MATCHES "GYANISSCRIBE_HAS_TESSERACT")
    target_sources(TestOcr PRIVATE TestTesseractOcrEngine.cpp)
    # 这份用例认的是随仓库那一份语言包，路径由构建期给；装完即用的判据不该依赖用户把包放哪
    target_compile_definitions(TestOcr PRIVATE GYANISSCRIBE_VENDORED_TESSDATA="${CMAKE_SOURCE_DIR}/vendor/Tessdata")
endif ()

target_link_libraries(TestOcr PRIVATE
        Ocr
        Settings
        Platform
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestOcr PRIVATE /W4 /WX)
endif ()

add_dependencies(TestOcr DeployQtRuntime)

include(GoogleTest)
# 识别结果复制那几条借系统剪贴板传文本：并行时读到的会是别的进程刚写进去的内容。
# 锁名与 TestPin / TestCapture 的是同一把：跨二进制的互斥只在两边都拿它时成立。
gtest_discover_tests(TestOcr PROPERTIES RESOURCE_LOCK clipboardShared)
