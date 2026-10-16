# Core/Image：图像工具（等比适配、格式编解码、成品装饰、剪贴板文件、读回一张图）
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/ImageFit.h
        ${CMAKE_CURRENT_LIST_DIR}/ImageFit.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ImageFormat.h
        ${CMAKE_CURRENT_LIST_DIR}/ImageFormat.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ImageLoading.h
        ${CMAKE_CURRENT_LIST_DIR}/ImageLoading.cpp
        ${CMAKE_CURRENT_LIST_DIR}/OutputDecoration.h
        ${CMAKE_CURRENT_LIST_DIR}/OutputDecoration.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ClipboardFile.h
        ${CMAKE_CURRENT_LIST_DIR}/ClipboardFile.cpp)
