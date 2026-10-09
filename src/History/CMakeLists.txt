# History：清单的落盘、保留策略与筛选。这里只做「记录」，成品文件始终留在用户选的目录里
add_library(History STATIC
        HistoryTypes.h
        HistoryTypes.cpp
        HistoryRules.h
        HistoryRules.cpp
        HistoryStore.h
        HistoryStore.cpp)

target_include_directories(History PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(History PUBLIC
        Core
        Settings
        Qt6::Core)

if (MSVC)
    target_compile_options(History PRIVATE /W4 /WX)
endif ()
