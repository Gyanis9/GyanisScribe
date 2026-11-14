# Plugins：可替换后端实现（条码、抓帧来源、识别引擎、录屏编码各占一个子目录）；
# 接口在 Core，实现在这里，装配层（App/Main）决定用哪一个

# ---- 条件先定，叶子目录按标记决定收哪些源文件 ----
# Barcode：zxing-cpp（Apache-2.0，许可证正文随包发）。找不到时函数交回 DependencyMissing，
# 降级分支与真后端是同一个源文件里的两半，都编得到。
find_package(ZXing QUIET CONFIG)
if (ZXing_FOUND)
    set(GYANISSCRIBE_HAS_ZXING ON)
endif ()

# Windows 系统识别走系统组件 Windows.Media.Ocr：它不是可选依赖而是平台条件——
# 投影头 vendor 在 vendor/WinRt，只有 Windows + MSVC 这一档编得到。
if (WIN32 AND MSVC)
    set(GYANISSCRIBE_HAS_WINRT_OCR ON)
endif ()

# PP-OCR 的模型与 ONNX Runtime 都随仓库分发，是硬依赖而不是可选包：引擎始终收进构建，
# 条件宏与选中 WinRT 的那条同形，留给叶子目录按标记收源文件
set(GYANISSCRIBE_HAS_PPOCR ON)

add_subdirectory(Barcode)
add_subdirectory(CaptureSources)
add_subdirectory(OcrEngines)
add_subdirectory(Recording)

get_property(PluginsSources GLOBAL PROPERTY GyanisScribePluginsSources)
add_library(Plugins STATIC ${PluginsSources})

target_include_directories(Plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# ONNX Runtime 的 C API 头 vendor 在仓库里：SYSTEM + /external:W0，头文件自身的告警不进零告警门禁
target_include_directories(Plugins SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/vendor/OnnxRuntime/include")
# 宏发给所有消费者：用例据此决定收不收这一份直测，工厂后续按它判断后端有没有编进来
target_compile_definitions(Plugins PUBLIC GYANISSCRIBE_HAS_PPOCR=1)
target_link_libraries(Plugins PUBLIC
        Core
        Platform
        Qt6::Core
        Qt6::Gui)

if (GYANISSCRIBE_HAS_ZXING)
    target_link_libraries(Plugins PRIVATE ZXing::ZXing)
    # 宏发给所有消费者：界面拿它决定按钮灰不灰、用例拿它决定断言哪一档，两处各写一份判断就会分家
    target_compile_definitions(Plugins PUBLIC GYANISSCRIBE_HAS_ZXING=1)
endif ()

# DXGI 桌面重复是 Windows + MSVC 这一档的实现：条件分支由平台决定，
# 而不是「可选的第三方依赖」（与 Ocr 的 WinRT 后端同一条规矩）
if (WIN32 AND MSVC)
    # com_ptr 这些投影头 vendor 在 vendor/WinRt：SYSTEM + /external:W0，生成头里的告警不进零告警门禁
    target_include_directories(Plugins SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/WinRt")
    target_compile_options(Plugins PRIVATE /external:W0)
    # 桌面重复的接口在 dxgi，设备与暂存纹理在 d3d11，winrt 运行时伞库随系统
    target_link_libraries(Plugins PRIVATE d3d11 dxgi windowsapp)
    # 宏要发给所有消费者：工厂「PP-OCR 之后问系统」这件事在用例里是可观察的断言，
    # 用例必须知道这一档到底有没有这份后端（与 GYANISSCRIBE_HAS_PPOCR 同一条理由）
    target_compile_definitions(Plugins PUBLIC GYANISSCRIBE_HAS_WINRT_OCR=1)
endif ()

if (MSVC)
    target_compile_options(Plugins PRIVATE /W4 /WX)
endif ()
