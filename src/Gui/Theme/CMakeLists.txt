# Gui/Theme：设计令牌、主题档位与管理器、界面语言
set_property(GLOBAL APPEND PROPERTY GyanisScribeGuiSources
        ${CMAKE_CURRENT_LIST_DIR}/DesignTokens.h
        ${CMAKE_CURRENT_LIST_DIR}/ThemeKind.h
        ${CMAKE_CURRENT_LIST_DIR}/ThemeKind.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ThemeManager.h
        ${CMAKE_CURRENT_LIST_DIR}/ThemeManager.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Language.h
        ${CMAKE_CURRENT_LIST_DIR}/Language.cpp)
