# Core/Steps：步骤记录的值类型、自动说明与会话存储；不依赖 Widgets/Plugins
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/StepTypes.h
        ${CMAKE_CURRENT_LIST_DIR}/StepSession.h
        ${CMAKE_CURRENT_LIST_DIR}/StepSession.cpp)
