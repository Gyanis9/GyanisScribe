# Ui：设计令牌（纯常量）、主题档位与管理器、图标供给器（要读 qrc，所以是真目标文件）
add_library(Ui STATIC
        DesignTokens.h
        KeyStep.h
        KeyStep.cpp
        ThemeKind.h
        ThemeKind.cpp
        Language.h
        Language.cpp
        ThemeManager.h
        ThemeManager.cpp
        AppResources.h
        AppResources.cpp
        HotkeyEdit.h
        HotkeyEdit.cpp
        IconProvider.h
        IconProvider.cpp
        Toast.h
        Toast.cpp
        ${CMAKE_SOURCE_DIR}/Resources/AppResources.qrc)

# qrc 由 AUTORCC 生成一个带静态初始化器的 .cpp；静态库里这份没人引用就会被链接器丢掉，
# 于是运行时 :/icons 是空的，所以 IconProvider 构造时显式 Q_INIT_RESOURCE 一次
set_target_properties(Ui PROPERTIES AUTORCC ON)

target_include_directories(Ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# HotkeyEdit 要在校验组合键能否全局登记时才写配置，所以直接吃 Hotkey；
# Hotkey 只依赖 Core/Platform/Settings，反向不指回 Ui，加这条不会成环
target_link_libraries(Ui PUBLIC
        Core
        Settings
        Hotkey
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg)

if (MSVC)
    target_compile_options(Ui PRIVATE /W4 /WX)
endif ()
