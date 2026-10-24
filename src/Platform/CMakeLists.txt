# Platform：能力表与热键后端契约在任何平台都编；平台实现按编译目标挂载，
# 将来接别的平台时在下面加各自的分支。
set(PlatformCommonSources
        AnimationSettings.h
        AnimationSettings.cpp
        PlatformCapabilities.h
        IHotkeyBackend.h
        IPlatformIntegration.h
        PlatformIntegration.cpp
        UnsupportedHotkeyBackend.h
        UnsupportedHotkeyBackend.cpp
        WindowGeometry.h
        WindowGeometry.cpp
        CrashGuard.h
        CrashGuard.cpp)

add_library(Platform STATIC ${PlatformCommonSources})

target_include_directories(Platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# 平台层只用值类型/日志/文案（Base），不碰领域逻辑：这样 Core 侧的编排（截图/录屏）能
# 反向依赖 Platform 而不成环——服务都以接口引用注入，平台实现只在装配层构造。
target_link_libraries(Platform PUBLIC
        Base
        Qt6::Core
        Qt6::Gui)

if (MSVC)
    target_compile_options(Platform PRIVATE /W4 /WX)
endif ()

if (WIN32)
    target_sources(Platform PRIVATE
            Windows/WindowsIntegration.h
            Windows/WindowsIntegration.cpp
            Windows/Win32HotkeyBackend.h
            Windows/Win32HotkeyBackend.cpp
            Windows/Win32HotkeyMapping.h
            Windows/Win32HotkeyMapping.cpp
            Windows/Win32Autostart.h
            Windows/Win32Autostart.cpp)

    # 窗口枚举要用 DwmGetWindowAttribute 取「实际可见的那圈边界」，它在 dwmapi 里；
    # user32 由 Qt6::Gui 带进来，这一份没人给，不显式链就上 LNK2019
    target_link_libraries(Platform PRIVATE dwmapi)

    # 界面元素检测走 UI Automation：CLSID_CUIAutomation 与运行期那一份 COM 实现都在这个库里，
    # 不显式链就是 LNK2019
    target_link_libraries(Platform PRIVATE uiautomationcore)

    # 崩溃转储要 MiniDumpWriteDump，它在 dbghelp 里（Windows 才有；CrashGuard.cpp 里非 Windows
    # 那一条分支是空操作，所以这两行才敢只在 Windows 上写）
    target_link_libraries(Platform PRIVATE dbghelp)

endif ()
