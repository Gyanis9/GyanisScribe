# Core/Ocr：识别类型、预处理、导出、语言词表与「搬到工作线程」的编排
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/OcrTypes.h
        ${CMAKE_CURRENT_LIST_DIR}/OcrTypes.cpp
        ${CMAKE_CURRENT_LIST_DIR}/OcrPreprocessor.h
        ${CMAKE_CURRENT_LIST_DIR}/OcrPreprocessor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/OcrTextExporter.h
        ${CMAKE_CURRENT_LIST_DIR}/OcrTextExporter.cpp
        ${CMAKE_CURRENT_LIST_DIR}/OcrLanguage.h
        ${CMAKE_CURRENT_LIST_DIR}/OcrLanguage.cpp
        ${CMAKE_CURRENT_LIST_DIR}/IOcrEngine.h
        ${CMAKE_CURRENT_LIST_DIR}/RecognitionRunner.h
        ${CMAKE_CURRENT_LIST_DIR}/RecognitionRunner.cpp)
