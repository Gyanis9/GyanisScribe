# Core/Privacy：本地敏感信息检测的类型与纯逻辑；不依赖 Widgets/Plugins
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/PiiTypes.h
        ${CMAKE_CURRENT_LIST_DIR}/PiiDetector.h
        ${CMAKE_CURRENT_LIST_DIR}/PiiDetector.cpp)
