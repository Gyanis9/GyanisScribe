# Capture：选区换算（纯函数）+ 抓帧来源 + 编排 + 遮罩
add_library(Capture STATIC
        ScreenUtils.h
        ScreenUtils.cpp
        FrameStitch.h
        FrameStitch.cpp
        LongCaptureHud.h
        LongCaptureHud.cpp
        IScreenCaptureSource.h
        QScreenCaptureSource.h
        QScreenCaptureSource.cpp
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

if (MSVC)
    target_compile_options(Capture PRIVATE /W4 /WX)
endif ()
