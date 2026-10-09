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

if (MSVC)
    target_compile_options(Ocr PRIVATE /W4 /WX)
endif ()
