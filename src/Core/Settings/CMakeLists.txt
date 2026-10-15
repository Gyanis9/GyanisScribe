# Core/Settings：配置存储（ISettingsStore 契约、SettingsStore 实现、命名与输出样式规则、键名清册）
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/ISettingsStore.h
        ${CMAKE_CURRENT_LIST_DIR}/NamingRules.h
        ${CMAKE_CURRENT_LIST_DIR}/NamingRules.cpp
        ${CMAKE_CURRENT_LIST_DIR}/OutputStyleRules.h
        ${CMAKE_CURRENT_LIST_DIR}/OutputStyleRules.cpp
        ${CMAKE_CURRENT_LIST_DIR}/SettingsStore.h
        ${CMAKE_CURRENT_LIST_DIR}/SettingsStore.cpp
        ${CMAKE_CURRENT_LIST_DIR}/SettingsKeys.h)
