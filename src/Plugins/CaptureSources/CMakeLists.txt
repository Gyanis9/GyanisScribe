# Plugins/CaptureSources：抓帧来源实现与选型工厂（DXGI 桌面重复是 Windows + MSVC 那一档）
set_property(GLOBAL APPEND PROPERTY GyanisScribePluginsSources
        ${CMAKE_CURRENT_LIST_DIR}/QScreenCaptureSource.h
        ${CMAKE_CURRENT_LIST_DIR}/QScreenCaptureSource.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ScreenCaptureSourceFactory.h
        ${CMAKE_CURRENT_LIST_DIR}/ScreenCaptureSourceFactory.cpp
        ${CMAKE_CURRENT_LIST_DIR}/DxgiCaptureSource.h
        ${CMAKE_CURRENT_LIST_DIR}/DxgiCaptureSource.cpp)
