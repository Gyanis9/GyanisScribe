# Core/Share：美化分享的样式值、内置模板与本地模板目录（纯逻辑，不渲染）
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/ShareStyle.h
        ${CMAKE_CURRENT_LIST_DIR}/ShareStyle.cpp)
