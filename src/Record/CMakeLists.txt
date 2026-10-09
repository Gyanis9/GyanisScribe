# Record：录屏。参数校验、节拍、会话状态与命令行拼接都是纯逻辑，容器里无界面也能测；
# 只有 RecordingService 那一层胶水要真桌面会话，端到端行为留给验收
add_library(Record STATIC
        RecorderConfig.h
        RecorderConfig.cpp
        RecordingTiming.h
        RecordingTiming.cpp
        RecordingSinks.h
        RecordingSinks.cpp
        FfmpegCommand.h
        FfmpegCommand.cpp
        FfmpegRecordingEncoder.h
        FfmpegRecordingEncoder.cpp
        RecordingSession.h
        RecordingSession.cpp
        RecordingService.h
        RecordingService.cpp)

target_include_directories(Record PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Record PUBLIC
        Capture
        Core
        Settings
        Qt6::Core
        Qt6::Gui)

if (MSVC)
    target_compile_options(Record PRIVATE /W4 /WX)
endif ()
