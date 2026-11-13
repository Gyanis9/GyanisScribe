# Core/Content：识别结果的表格检测、CSV 与 Markdown 纯逻辑
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/ContentTypes.h
        ${CMAKE_CURRENT_LIST_DIR}/CsvWriter.h
        ${CMAKE_CURRENT_LIST_DIR}/CsvWriter.cpp
        ${CMAKE_CURRENT_LIST_DIR}/TableExportRules.h
        ${CMAKE_CURRENT_LIST_DIR}/TableExportRules.cpp
        ${CMAKE_CURRENT_LIST_DIR}/XlsxWriter.h
        ${CMAKE_CURRENT_LIST_DIR}/XlsxWriter.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ZipStoreWriter.h
        ${CMAKE_CURRENT_LIST_DIR}/ZipStoreWriter.cpp
        ${CMAKE_CURRENT_LIST_DIR}/MarkdownBuilder.h
        ${CMAKE_CURRENT_LIST_DIR}/MarkdownBuilder.cpp
        ${CMAKE_CURRENT_LIST_DIR}/TableDetector.h
        ${CMAKE_CURRENT_LIST_DIR}/TableDetector.cpp)
