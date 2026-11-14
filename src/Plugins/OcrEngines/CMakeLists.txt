# Plugins/OcrEngines：识别引擎实现与选型工厂（PaddleOCR 与 WinRT 两份按构建条件收）
set_property(GLOBAL APPEND PROPERTY GyanisScribePluginsSources
        ${CMAKE_CURRENT_LIST_DIR}/OcrEngineFactory.h
        ${CMAKE_CURRENT_LIST_DIR}/OcrEngineFactory.cpp
        ${CMAKE_CURRENT_LIST_DIR}/UnavailableOcrEngine.h
        ${CMAKE_CURRENT_LIST_DIR}/UnavailableOcrEngine.cpp)
if (GYANISSCRIBE_HAS_WINRT_OCR)
    set_property(GLOBAL APPEND PROPERTY GyanisScribePluginsSources
            ${CMAKE_CURRENT_LIST_DIR}/WinRTImageOcrEngine.h
            ${CMAKE_CURRENT_LIST_DIR}/WinRTImageOcrEngine.cpp)
endif ()
if (GYANISSCRIBE_HAS_PPOCR)
    set_property(GLOBAL APPEND PROPERTY GyanisScribePluginsSources
            ${CMAKE_CURRENT_LIST_DIR}/PpOcrOnnxEngine.h
            ${CMAKE_CURRENT_LIST_DIR}/PpOcrOnnxEngine.cpp)
endif ()
