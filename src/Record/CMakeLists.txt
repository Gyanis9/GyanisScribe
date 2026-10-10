# Record：录屏。参数校验、节拍、会话状态与命令行拼接都是纯逻辑，容器里无界面也能测；
# 只有 RecordingService 那一层胶水要真桌面会话，端到端行为留给验收
add_library(Record STATIC
        RecorderConfig.h
        RecorderConfig.cpp
        RecordingTiming.h
        RecordingTiming.cpp
        RecordingSinks.h
        RecordingSinks.cpp
        CursorHighlight.h
        CursorHighlight.cpp
        FfmpegCommand.h
        FfmpegCommand.cpp
        FfmpegAudioDevices.h
        FfmpegAudioDevices.cpp
        FfmpegVideoEncoders.h
        FfmpegVideoEncoders.cpp
        FfmpegRecordingEncoder.h
        FfmpegRecordingEncoder.cpp
        RecordingSession.h
        RecordingSession.cpp
        RecordingService.h
        RecordingService.cpp
        RecordingHud.h
        RecordingHud.cpp)

target_include_directories(Record PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# 控制条要图标与窗口，所以 Record 吃 Ui 与 Widgets
# Concurrent 给 stop() 那一步用：把 ffmpeg 编码搬到线程池，界面在编码期间不再停住
target_link_libraries(Record PUBLIC
        Capture
        Core
        Settings
        Ui
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent)

if (MSVC)
    target_compile_options(Record PRIVATE /W4 /WX)
endif ()
