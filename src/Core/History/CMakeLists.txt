# Core/History：历史存储（清单、保留策略、缩略图、未保存归档）
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/HistoryRules.h
        ${CMAKE_CURRENT_LIST_DIR}/HistoryRules.cpp
        ${CMAKE_CURRENT_LIST_DIR}/HistoryStore.h
        ${CMAKE_CURRENT_LIST_DIR}/HistoryStore.cpp
        ${CMAKE_CURRENT_LIST_DIR}/HistoryTypes.h
        ${CMAKE_CURRENT_LIST_DIR}/HistoryTypes.cpp
        ${CMAKE_CURRENT_LIST_DIR}/HistoryThumbnail.h
        ${CMAKE_CURRENT_LIST_DIR}/HistoryThumbnail.cpp
        ${CMAKE_CURRENT_LIST_DIR}/UnsavedArchive.h
        ${CMAKE_CURRENT_LIST_DIR}/UnsavedArchive.cpp)
