# Core/BugReport：报告环境、字段与 Markdown 的纯逻辑；不依赖 Widgets
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/BugReport.h
        ${CMAKE_CURRENT_LIST_DIR}/BugReport.cpp)
