# Plugins/Barcode：条码识别后端（zxing-cpp，可选依赖）
set_property(GLOBAL APPEND PROPERTY GyanisScribePluginsSources
        ${CMAKE_CURRENT_LIST_DIR}/BarcodeDecoder.h
        ${CMAKE_CURRENT_LIST_DIR}/BarcodeDecoder.cpp)
