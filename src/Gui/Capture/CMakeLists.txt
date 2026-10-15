# Gui/Capture：遮罩、长截图控制条与截图编排（编排要建遮罩窗与控制条，故随窗口侧）
set_property(GLOBAL APPEND PROPERTY GyanisScribeGuiSources
        ${CMAKE_CURRENT_LIST_DIR}/CaptureOverlay.h
        ${CMAKE_CURRENT_LIST_DIR}/CaptureOverlay.cpp
        ${CMAKE_CURRENT_LIST_DIR}/LongCaptureHud.h
        ${CMAKE_CURRENT_LIST_DIR}/LongCaptureHud.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CaptureService.h
        ${CMAKE_CURRENT_LIST_DIR}/CaptureService.cpp)
