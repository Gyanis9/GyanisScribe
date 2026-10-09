# Ocr：类型、预处理、导出与后端选型。识别本身是可选依赖：
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
        UnavailableOcrEngine.h
        UnavailableOcrEngine.cpp)

add_library(Ocr STATIC ${OcrSources})

target_include_directories(Ocr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Ocr PUBLIC
        Core
        Platform
        Settings
        Qt6::Core
        Qt6::Gui)

if (MSVC)
    target_compile_options(Ocr PRIVATE /W4 /WX)
endif ()
