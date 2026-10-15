# Gui：通用 UI 底座（主题/控件/资源）与各功能窗口；各子目录在叶子 CMakeLists 里登记源文件，
# 这里只做聚合与对外接口
add_subdirectory(Theme)
add_subdirectory(Controls)
add_subdirectory(Resources)
add_subdirectory(Pick)
add_subdirectory(History)
add_subdirectory(Pin)
add_subdirectory(Capture)
add_subdirectory(Ocr)
add_subdirectory(Record)

get_property(GuiSources GLOBAL PROPERTY GyanisScribeGuiSources)
add_library(Gui STATIC ${GuiSources})

# qrc 由 AUTORCC 生成一个带静态初始化器的 .cpp；静态库里这份没人引用就会被链接器丢掉，
# 于是运行时 :/icons 是空的，所以 IconProvider 构造时显式 Q_INIT_RESOURCE 一次
set_target_properties(Gui PROPERTIES AUTORCC ON)

target_include_directories(Gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# HotkeyEdit 要在校验组合键能否全局登记时才写配置：热键管理在 Core，热键后端在 Platform；
# 贴图窗口族（穿透/置顶/吸附）同样要用平台能力，所以 Platform 直接吃在这里。
# Annotation 是临时依赖：贴图窗的「直接标注」用标注工具与视图；批 16 标注并入 Gui/Core 后撤掉。
target_link_libraries(Gui PUBLIC
        Annotation
        Core
        Platform
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg)

if (MSVC)
    target_compile_options(Gui PRIVATE /W4 /WX)
endif ()
