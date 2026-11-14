add_executable(TestPluginsOcrEngines
        TestOcrEngines.cpp
        TestPpOcrOnnxEngine.cpp)

# 这份用例认的是随仓库那一份模型，路径由构建期给；运行库走 ctest 的环境变量，
# 与产品里「先看环境变量、再看程序目录」的查找顺序是同一套口径
target_compile_definitions(TestPluginsOcrEngines PRIVATE GYANISSCRIBE_VENDORED_PPOCR="${CMAKE_SOURCE_DIR}/vendor/PpocrModels")

# 系统识别后端的直测跟着库那一档的条件走（同一台机器、同一个编译器才有这份后端）；
# 判据直接问库导出的宏，不在这里重复一遍平台判断
get_target_property(pluginsInterfaceCompileDefinitions Plugins INTERFACE_COMPILE_DEFINITIONS)
if (pluginsInterfaceCompileDefinitions MATCHES "GYANISSCRIBE_HAS_WINRT_OCR")
    target_sources(TestPluginsOcrEngines PRIVATE TestWinRTImageOcrEngine.cpp)
endif ()

target_link_libraries(TestPluginsOcrEngines PRIVATE
        Plugins
        Core
        Platform
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestPluginsOcrEngines PRIVATE /W4 /WX)
endif ()

add_dependencies(TestPluginsOcrEngines DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestPluginsOcrEngines PROPERTIES ENVIRONMENT "GYANISSCRIBE_ONNXRUNTIME_DLL=${CMAKE_SOURCE_DIR}/vendor/OnnxRuntime/bin/onnxruntime.dll")
