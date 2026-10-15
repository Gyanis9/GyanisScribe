# Core/Hotkey：组合键解析与全局登记
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/HotkeyCombination.h
        ${CMAKE_CURRENT_LIST_DIR}/HotkeyCombination.cpp
        ${CMAKE_CURRENT_LIST_DIR}/IHotkeyManager.h
        ${CMAKE_CURRENT_LIST_DIR}/HotkeyManager.h
        ${CMAKE_CURRENT_LIST_DIR}/HotkeyManager.cpp)
