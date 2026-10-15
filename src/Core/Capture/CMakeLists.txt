# Core/Capture：选区换算、区域历史、拼帧、指针像素、BGRA 帧与两个接口（抓帧来源/截图编排）
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/ScreenUtils.h
        ${CMAKE_CURRENT_LIST_DIR}/ScreenUtils.cpp
        ${CMAKE_CURRENT_LIST_DIR}/RegionHistory.h
        ${CMAKE_CURRENT_LIST_DIR}/RegionHistory.cpp
        ${CMAKE_CURRENT_LIST_DIR}/FrameStitch.h
        ${CMAKE_CURRENT_LIST_DIR}/FrameStitch.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CursorPixels.h
        ${CMAKE_CURRENT_LIST_DIR}/CursorPixels.cpp
        ${CMAKE_CURRENT_LIST_DIR}/BgraFrame.h
        ${CMAKE_CURRENT_LIST_DIR}/BgraFrame.cpp
        ${CMAKE_CURRENT_LIST_DIR}/IScreenCaptureSource.h
        ${CMAKE_CURRENT_LIST_DIR}/ICaptureService.h)
