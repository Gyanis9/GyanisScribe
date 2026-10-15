# Hotkey：组合键解析与登记编排。这里不出现任何操作系统头，跨平台判据靠注入的后端替身跑
add_library(Hotkey STATIC
        HotkeyCombination.h
        HotkeyCombination.cpp
        IHotkeyManager.h
        HotkeyManager.h
        HotkeyManager.cpp)

target_include_directories(Hotkey PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Hotkey PUBLIC
        Core
        Platform
        Qt6::Core)

if (MSVC)
    target_compile_options(Hotkey PRIVATE /W4 /WX)
endif ()
