# Plugins/Recording：ffmpeg 子进程封装（命令拼装、编码器探测、音频设备枚举）
set_property(GLOBAL APPEND PROPERTY GyanisScribePluginsSources
        ${CMAKE_CURRENT_LIST_DIR}/RecordingService.h
        ${CMAKE_CURRENT_LIST_DIR}/RecordingService.cpp
        ${CMAKE_CURRENT_LIST_DIR}/FfmpegAudioDevices.h
        ${CMAKE_CURRENT_LIST_DIR}/FfmpegAudioDevices.cpp
        ${CMAKE_CURRENT_LIST_DIR}/FfmpegCommand.h
        ${CMAKE_CURRENT_LIST_DIR}/FfmpegCommand.cpp
        ${CMAKE_CURRENT_LIST_DIR}/FfmpegRecordingEncoder.h
        ${CMAKE_CURRENT_LIST_DIR}/FfmpegRecordingEncoder.cpp
        ${CMAKE_CURRENT_LIST_DIR}/FfmpegVideoEncoders.h
        ${CMAKE_CURRENT_LIST_DIR}/FfmpegVideoEncoders.cpp
        ${CMAKE_CURRENT_LIST_DIR}/FfmpegVideoEncoderEnumerator.h
        ${CMAKE_CURRENT_LIST_DIR}/FfmpegVideoEncoderEnumerator.cpp)
