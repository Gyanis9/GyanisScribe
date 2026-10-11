# Annotation：图形记录、各档工具与引擎。绘制只依赖 QPainter/QImage，容器里无界面也能测
add_library(Annotation STATIC
        AnnotationShape.h
        AnnotationGeometry.h
        AnnotationTool.h
        AnnotationTool.cpp
        RecentColors.h
        RecentColors.cpp
        RectangleTool.h
        RectangleTool.cpp
        EllipseTool.h
        EllipseTool.cpp
        ArrowTool.h
        ArrowTool.cpp
        PenTool.h
        PenTool.cpp
        HighlightTool.h
        HighlightTool.cpp
        MosaicTool.h
        MosaicTool.cpp
        ImageBlur.h
        ImageBlur.cpp
        BlurTool.h
        BlurTool.cpp
        EraserTool.h
        EraserTool.cpp
        TextTool.h
        TextTool.cpp
        NumberTool.h
        LineTool.h
        LineTool.cpp
        NumberTool.cpp
        AnnotationToolbarGeometry.h
        AnnotationToolbarGeometry.cpp
        AnnotationToolbar.h
        AnnotationToolbar.cpp
        AnnotationView.h
        AnnotationView.cpp
        AnnotationWindow.h
        AnnotationWindow.cpp
        IAnnotationEngine.h
        AnnotationEngine.h
        AnnotationEngine.cpp)

target_include_directories(Annotation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Annotation PUBLIC
        Core
        Settings
        Ui
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(Annotation PRIVATE /W4 /WX)
endif ()
