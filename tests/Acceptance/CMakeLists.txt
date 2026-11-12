add_executable(AcceptanceTool
        AcceptanceTool.cpp)

# Plugins 提供 OCR 工厂；Gui 提供 QImage/QPainter 的绘制后端
target_link_libraries(AcceptanceTool PRIVATE
        Core
        Platform
        Plugins
        Qt6::Core
        Qt6::Gui)

if (MSVC)
    target_compile_options(AcceptanceTool PRIVATE /W4 /WX)
endif ()

add_dependencies(AcceptanceTool DeployQtRuntime)
