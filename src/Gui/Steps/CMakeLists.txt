# Gui/Steps：步骤记录的控制条、首次说明、审核窗与 PDF 导出
set_property(GLOBAL APPEND PROPERTY GyanisScribeGuiSources
        ${CMAKE_CURRENT_LIST_DIR}/StepRecorderHud.h
        ${CMAKE_CURRENT_LIST_DIR}/StepRecorderHud.cpp
        ${CMAKE_CURRENT_LIST_DIR}/StepConsentDialog.h
        ${CMAKE_CURRENT_LIST_DIR}/StepConsentDialog.cpp
        ${CMAKE_CURRENT_LIST_DIR}/StepPdfExporter.h
        ${CMAKE_CURRENT_LIST_DIR}/StepPdfExporter.cpp
        ${CMAKE_CURRENT_LIST_DIR}/StepReviewWindow.h
        ${CMAKE_CURRENT_LIST_DIR}/StepReviewWindow.cpp)
