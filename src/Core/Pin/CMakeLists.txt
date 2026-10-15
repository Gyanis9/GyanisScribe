# Core/Pin：贴图算法（布局、变换、过滤、窗口接口）
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/IPinWindow.h
        ${CMAKE_CURRENT_LIST_DIR}/PinLayout.h
        ${CMAKE_CURRENT_LIST_DIR}/PinLayout.cpp
        ${CMAKE_CURRENT_LIST_DIR}/PinTransform.h
        ${CMAKE_CURRENT_LIST_DIR}/PinTransform.cpp
        ${CMAKE_CURRENT_LIST_DIR}/PinViewFilter.h
        ${CMAKE_CURRENT_LIST_DIR}/PinViewFilter.cpp)
