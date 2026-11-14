# Gui/Theme：设计令牌、主题档位与管理器、界面语言
set_property(GLOBAL APPEND PROPERTY GyanisScribeGuiSources
        ${CMAKE_CURRENT_LIST_DIR}/DesignTokens.h
        ${CMAKE_CURRENT_LIST_DIR}/CornerStyle.h
        ${CMAKE_CURRENT_LIST_DIR}/CornerStyle.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ControlPalette.h
        ${CMAKE_CURRENT_LIST_DIR}/ControlPalette.cpp
        ${CMAKE_CURRENT_LIST_DIR}/SquircleStyle.h
        ${CMAKE_CURRENT_LIST_DIR}/SquircleStyle.cpp
        ${CMAKE_CURRENT_LIST_DIR}/PopupSurface.h
        ${CMAKE_CURRENT_LIST_DIR}/PopupSurface.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ThemeKind.h
        ${CMAKE_CURRENT_LIST_DIR}/ThemeKind.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ThemeManager.h
        ${CMAKE_CURRENT_LIST_DIR}/ThemeManager.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Language.h
        ${CMAKE_CURRENT_LIST_DIR}/Language.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Motion.h
        ${CMAKE_CURRENT_LIST_DIR}/Motion.cpp)
