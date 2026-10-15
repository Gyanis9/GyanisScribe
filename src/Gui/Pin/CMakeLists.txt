# Gui/Pin：贴图窗口族（窗口、组管理窗、色卡与管理器；管理器持有真窗口，故随窗口侧）
set_property(GLOBAL APPEND PROPERTY GyanisScribeGuiSources
        ${CMAKE_CURRENT_LIST_DIR}/PinManager.h
        ${CMAKE_CURRENT_LIST_DIR}/PinManager.cpp
        ${CMAKE_CURRENT_LIST_DIR}/PinWindow.h
        ${CMAKE_CURRENT_LIST_DIR}/PinWindow.cpp
        ${CMAKE_CURRENT_LIST_DIR}/PinGroupManagerWindow.h
        ${CMAKE_CURRENT_LIST_DIR}/PinGroupManagerWindow.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ColorCard.h
        ${CMAKE_CURRENT_LIST_DIR}/ColorCard.cpp)
