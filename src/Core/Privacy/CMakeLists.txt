# Core/Privacy：本地敏感信息检测的类型与纯逻辑；不依赖 Widgets/Plugins
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/PiiTypes.h
        ${CMAKE_CURRENT_LIST_DIR}/PiiDetector.h
        ${CMAKE_CURRENT_LIST_DIR}/PiiDetector.cpp
        ${CMAKE_CURRENT_LIST_DIR}/PrivacyScanTiles.h
        ${CMAKE_CURRENT_LIST_DIR}/PrivacyScanTiles.cpp
        ${CMAKE_CURRENT_LIST_DIR}/RedactionGeometry.h
        ${CMAKE_CURRENT_LIST_DIR}/RedactionGeometry.cpp
        ${CMAKE_CURRENT_LIST_DIR}/RedactImage.h
        ${CMAKE_CURRENT_LIST_DIR}/RedactImage.cpp)
