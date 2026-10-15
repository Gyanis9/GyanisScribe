# Gui：通用 UI 底座（主题/控件/资源）与各功能窗口；各子目录在叶子 CMakeLists 里登记源文件，
# 这里只做聚合与对外接口
add_subdirectory(Theme)
add_subdirectory(Controls)
add_subdirectory(Resources)

get_property(GuiSources GLOBAL PROPERTY GyanisScribeGuiSources)
add_library(Gui STATIC ${GuiSources})

# qrc 由 AUTORCC 生成一个带静态初始化器的 .cpp；静态库里这份没人引用就会被链接器丢掉，
# 于是运行时 :/icons 是空的，所以 IconProvider 构造时显式 Q_INIT_RESOURCE 一次
set_target_properties(Gui PROPERTIES AUTORCC ON)

target_include_directories(Gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# HotkeyEdit 要在校验组合键能否全局登记时才写配置，所以直接吃 Hotkey；
# Hotkey 只依赖 Base/Platform/Core，反向不指回 Gui，加这条不会成环
target_link_libraries(Gui PUBLIC
        Core
        Hotkey
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg)

if (MSVC)
    target_compile_options(Gui PRIVATE /W4 /WX)
endif ()
