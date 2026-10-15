# Core/Record：录屏编排（配置、会话、节拍、帧落盘、光标高亮与编码接口）
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/RecorderConfig.h
        ${CMAKE_CURRENT_LIST_DIR}/RecorderConfig.cpp
        ${CMAKE_CURRENT_LIST_DIR}/RecordingSession.h
        ${CMAKE_CURRENT_LIST_DIR}/RecordingSession.cpp
        ${CMAKE_CURRENT_LIST_DIR}/RecordingTiming.h
        ${CMAKE_CURRENT_LIST_DIR}/RecordingTiming.cpp
        ${CMAKE_CURRENT_LIST_DIR}/RecordingSinks.h
        ${CMAKE_CURRENT_LIST_DIR}/RecordingSinks.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CursorHighlight.h
        ${CMAKE_CURRENT_LIST_DIR}/CursorHighlight.cpp
        ${CMAKE_CURRENT_LIST_DIR}/IRecordingEncoder.h)
