# Gui/Annotation：工具栏、工具条桥、布局几何、标注视图与标注窗
set_property(GLOBAL APPEND PROPERTY GyanisScribeGuiSources
        ${CMAKE_CURRENT_LIST_DIR}/AnnotationToolbar.cpp
        ${CMAKE_CURRENT_LIST_DIR}/AnnotationToolbar.h
        ${CMAKE_CURRENT_LIST_DIR}/AnnotationToolbarBridge.cpp
        ${CMAKE_CURRENT_LIST_DIR}/AnnotationToolbarBridge.h
        ${CMAKE_CURRENT_LIST_DIR}/AnnotationToolbarGeometry.cpp
        ${CMAKE_CURRENT_LIST_DIR}/AnnotationToolbarGeometry.h
        ${CMAKE_CURRENT_LIST_DIR}/AnnotationView.cpp
        ${CMAKE_CURRENT_LIST_DIR}/AnnotationView.h
        ${CMAKE_CURRENT_LIST_DIR}/AnnotationWindow.cpp
        ${CMAKE_CURRENT_LIST_DIR}/AnnotationWindow.h)
