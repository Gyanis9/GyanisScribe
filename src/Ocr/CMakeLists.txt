# Ocr：类型、预处理、导出、结果窗口与后端选型。识别本身是可选依赖：
# 找到 Tesseract 才编那一份后端，找不到时用 UnavailableOcrEngine 那份降级桩（它一直在编译列表里，
# 所以「缺失分支」不是从未被编过的分支）
set(OcrSources
        OcrTypes.h
        OcrTypes.cpp
        OcrPreprocessor.h
        OcrPreprocessor.cpp
        IOcrEngine.h
        OcrTextExporter.h
        OcrTextExporter.cpp
        OcrEngineFactory.h
        OcrEngineFactory.cpp
        OcrResultWindow.h
        OcrResultWindow.cpp
        UnavailableOcrEngine.h
        UnavailableOcrEngine.cpp)

# Tesseract 只在 Release 档进依赖图（上游 leptonica 的 Debug 包库名与 cpp_info 不一致，
# Debug 下 configure 必失败，详见 conanfile.py 与 docs/OcrBackends.md）。
# 找不到就是 Debug 档：那份降级桩一直在编译列表里，两种形状各被一个预设编到，不留没编过的分支。
find_package(Tesseract QUIET CONFIG)
if (Tesseract_FOUND)
    list(APPEND OcrSources
            TesseractOcrEngine.h
            TesseractOcrEngine.cpp)
endif ()

# Windows 系统识别走系统组件 Windows.Media.Ocr：它不是可选依赖而是平台条件——投影头 vendor 在
# Vendor/WinRt，只有 Windows + MSVC 这一档编得到，容器那一档既没有这套头也不需要它。
# 它也不依赖用户放语言包，所以 Debug 档（没有 Tesseract 的那一档）在 Windows 上从此有真后端。
if (WIN32 AND MSVC)
    list(APPEND OcrSources
            WinRTImageOcrEngine.h
            WinRTImageOcrEngine.cpp)
endif ()

add_library(Ocr STATIC ${OcrSources})

target_include_directories(Ocr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# 结果窗口要图标与主题色，所以 Ocr 吃 Ui；Ui 只依赖 Core/Settings/Hotkey，不指回 Ocr
target_link_libraries(Ocr PUBLIC
        Core
        Platform
        Settings
        Ui
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (Tesseract_FOUND)
    target_link_libraries(Ocr PUBLIC Tesseract::libtesseract)
    # 选型代码按这个宏决定「拿到真后端」还是「报后端没编进来」，宏必须发给所有消费者，
    # 否则界面层与工厂对同一个后端的判断会分家
    target_compile_definitions(Ocr PUBLIC GYANISSCRIBE_HAS_TESSERACT=1)
endif ()

if (WIN32 AND MSVC)
    # SYSTEM：vendor 的生成头在 /W4 /WX 下会报成百条警告，必须按外部头交给编译器（CMake 把它翻成 /external:I），
    # 再配 /external:W0 定外部头的告警档位——只挂 SYSTEM 不改档位，生成头里的警告照样算进零告警门禁
    target_include_directories(Ocr SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../Vendor/WinRt")
    target_compile_options(Ocr PRIVATE /external:W0)
    # 激活 WinRT 类要的伞导入库；它不随包分发，运行期用的是系统自带的 API
    target_link_libraries(Ocr PRIVATE windowsapp)
    # 宏要发给所有消费者：工厂「先问系统、再找 Tesseract」这件事在用例里是可观察的断言，
    # 用例必须知道这一档到底有没有这份后端（与 GYANISSCRIBE_HAS_TESSERACT 同一条理由）
    target_compile_definitions(Ocr PUBLIC GYANISSCRIBE_HAS_WINRT_OCR=1)
endif ()

if (MSVC)
    target_compile_options(Ocr PRIVATE /W4 /WX)
endif ()
