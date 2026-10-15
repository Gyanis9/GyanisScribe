# Plugins/OcrEngines：识别引擎实现与选型工厂（Tesseract 与 WinRT 两份按构建条件收）
set_property(GLOBAL APPEND PROPERTY GyanisScribePluginsSources
        ${CMAKE_CURRENT_LIST_DIR}/OcrEngineFactory.h
        ${CMAKE_CURRENT_LIST_DIR}/OcrEngineFactory.cpp
        ${CMAKE_CURRENT_LIST_DIR}/UnavailableOcrEngine.h
        ${CMAKE_CURRENT_LIST_DIR}/UnavailableOcrEngine.cpp)
if (GYANISSCRIBE_HAS_TESSERACT)
    set_property(GLOBAL APPEND PROPERTY GyanisScribePluginsSources
            ${CMAKE_CURRENT_LIST_DIR}/TesseractOcrEngine.h
            ${CMAKE_CURRENT_LIST_DIR}/TesseractOcrEngine.cpp)
endif ()
if (GYANISSCRIBE_HAS_WINRT_OCR)
    set_property(GLOBAL APPEND PROPERTY GyanisScribePluginsSources
            ${CMAKE_CURRENT_LIST_DIR}/WinRTImageOcrEngine.h
            ${CMAKE_CURRENT_LIST_DIR}/WinRTImageOcrEngine.cpp)
endif ()
