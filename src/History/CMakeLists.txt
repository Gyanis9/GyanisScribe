# History：清单的落盘、保留策略与筛选。这里只做「记录」，成品文件始终留在用户选的目录里
add_library(History STATIC
        HistoryTypes.h
        HistoryTypes.cpp
        HistoryRules.h
        HistoryRules.cpp
        HistoryStore.h
        HistoryStore.cpp
        HistoryThumbnail.h
        HistoryThumbnail.cpp
        UnsavedArchive.h
        UnsavedArchive.cpp
        HistoryView.h
        HistoryView.cpp)

target_include_directories(History PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# 历史页要缩略图与图标，所以吃 Ui 与 Widgets
target_link_libraries(History PUBLIC
        Core
        Settings
        Ui
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(History PRIVATE /W4 /WX)
endif ()
