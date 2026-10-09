# Platform：能力表与热键后端契约在任何平台都编；平台实现按编译目标二选一，
# 另一档另建一个「只编译不链接」的检查目标，免得那一份实现长期处于从未被编过的状态。
set(PlatformCommonSources
        PlatformCapabilities.h
        IHotkeyBackend.h
        IPlatformIntegration.h
        PlatformIntegration.cpp
        UnsupportedHotkeyBackend.h
        UnsupportedHotkeyBackend.cpp)

add_library(Platform STATIC ${PlatformCommonSources})

target_include_directories(Platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Platform PUBLIC
        Core
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
            Windows/Win32HotkeyMapping.cpp)

    # Linux 那一档在 Windows 上不会被选中，给它一条只编译不链接的形状检查：
    # 桩实现里一旦写了 Windows 才有的调用，这里当场编不过，而不是等到容器里才发现
    add_library(PlatformLinuxCheck OBJECT
            Linux/LinuxIntegration.h
            Linux/LinuxIntegration.cpp)
    target_link_libraries(PlatformLinuxCheck PRIVATE Platform)
    if (MSVC)
        target_compile_options(PlatformLinuxCheck PRIVATE /W4 /WX)
    endif ()
else ()
    target_sources(Platform PRIVATE
            Linux/LinuxIntegration.h
            Linux/LinuxIntegration.cpp)
endif ()
