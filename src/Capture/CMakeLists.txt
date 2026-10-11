# Capture：选区换算（纯函数）+ 抓帧来源 + 编排 + 遮罩
add_library(Capture STATIC
        ScreenUtils.h
        ScreenUtils.cpp
        FrameStitch.h
        FrameStitch.cpp
        CursorPixels.h
        CursorPixels.cpp
        LongCaptureHud.h
        LongCaptureHud.cpp
        IScreenCaptureSource.h
        BgraFrame.h
        BgraFrame.cpp
        QScreenCaptureSource.h
        QScreenCaptureSource.cpp
        ScreenCaptureSourceFactory.h
        ScreenCaptureSourceFactory.cpp
        ICaptureService.h
        CaptureService.h
        CaptureService.cpp
        CaptureOverlay.h
        CaptureOverlay.cpp)

target_include_directories(Capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Capture PUBLIC
        Core
        Pick
        Platform
        Settings
        Ui
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

# DXGI 桌面重复是 Windows + MSVC 这一档的实现：容器那一档既没有这套头也不需要它，
# 与 Ocr 的 WinRT 后端同一条规矩——条件分支由平台决定，而不是「可选的第三方依赖」
if (WIN32 AND MSVC)
    target_sources(Capture PRIVATE
            DxgiCaptureSource.h
            DxgiCaptureSource.cpp)
    # com_ptr 这些投影头 vendor 在 Vendor/WinRt：SYSTEM + /external:W0，生成头里的告警不进零告警门禁
    target_include_directories(Capture SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../Vendor/WinRt")
    target_compile_options(Capture PRIVATE /external:W0)
    # 桌面重复的接口在 dxgi，设备与暂存纹理在 d3d11，winrt 运行时伞库随系统
    target_link_libraries(Capture PRIVATE d3d11 dxgi windowsapp)
endif ()

if (MSVC)
    target_compile_options(Capture PRIVATE /W4 /WX)
endif ()
