# Gui/Controls：通用控件（Toast、快捷键输入框、按键步骤、界面快捷键清册）
set_property(GLOBAL APPEND PROPERTY GyanisScribeGuiSources
        ${CMAKE_CURRENT_LIST_DIR}/Toast.h
        ${CMAKE_CURRENT_LIST_DIR}/Toast.cpp
        ${CMAKE_CURRENT_LIST_DIR}/HotkeyEdit.h
        ${CMAKE_CURRENT_LIST_DIR}/HotkeyEdit.cpp
        ${CMAKE_CURRENT_LIST_DIR}/SpinBox.h
        ${CMAKE_CURRENT_LIST_DIR}/SpinBox.cpp
        ${CMAKE_CURRENT_LIST_DIR}/KeyStep.h
        ${CMAKE_CURRENT_LIST_DIR}/KeyStep.cpp
        ${CMAKE_CURRENT_LIST_DIR}/AppShortcuts.h
        ${CMAKE_CURRENT_LIST_DIR}/AppShortcuts.cpp
        ${CMAKE_CURRENT_LIST_DIR}/FocusRing.h
        ${CMAKE_CURRENT_LIST_DIR}/FocusRing.cpp)
