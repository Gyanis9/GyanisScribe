# Gui：通用 UI 底座（主题/控件/资源）与各功能窗口；各子目录在叶子 CMakeLists 里登记源文件，
# 这里只做聚合与对外接口
add_subdirectory(Theme)
add_subdirectory(Controls)
add_subdirectory(Resources)
add_subdirectory(Pick)
add_subdirectory(History)
add_subdirectory(Memory)
add_subdirectory(Pin)
add_subdirectory(Capture)
add_subdirectory(Ocr)
add_subdirectory(Privacy)
add_subdirectory(Record)
add_subdirectory(Annotation)
add_subdirectory(Rewind)
add_subdirectory(Steps)
add_subdirectory(Share)

get_property(GuiSources GLOBAL PROPERTY GyanisScribeGuiSources)
add_library(Gui STATIC ${GuiSources})

# qrc 由 AUTORCC 生成一个带静态初始化器的 .cpp；静态库里这份没人引用就会被链接器丢掉，
# 于是运行时 :/icons 是空的，所以 IconProvider 构造时显式 Q_INIT_RESOURCE 一次
set_target_properties(Gui PROPERTIES AUTORCC ON)

target_include_directories(Gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# HotkeyEdit 要在校验组合键能否全局登记时才写配置：热键管理在 Core，热键后端在 Platform；
# 贴图窗口族（穿透/置顶/吸附）同样要用平台能力，所以 Platform 直接吃在这里。
# Annotation 的界面件已并入本模块（Gui/Annotation）与 Core，不再单列依赖。
target_link_libraries(Gui PUBLIC
        Core
        Platform
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg)

# 只取 QtConcurrent 的头文件：QtConcurrent::run 的执行体跑在 QtCore 的线程池，运行目录不必摆一份没人加载的 Qt6Concurrent；
# Gui 显式按私有依赖（PRIVATE）链接它，因为目前只有 HistoryView.cpp 包含这个头，吃 Gui 的目标不必跟着继承；
# Core 眼下已经把 QtConcurrent 公开暴露给下游，这里的私有链接不改变那条既有依赖
target_link_libraries(Gui PRIVATE Qt6::Concurrent)

if (MSVC)
    target_compile_options(Gui PRIVATE /W4 /WX)
endif ()
