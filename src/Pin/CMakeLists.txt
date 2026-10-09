# Pin：贴图。纯几何与布局存取可无界面测试，窗口本身要显示会话，留给验收
add_library(Pin STATIC
        PinTransform.h
        PinTransform.cpp
        PinLayout.h
        PinLayout.cpp
        IPinWindow.h
        PinWindow.h
        PinWindow.cpp
        PinManager.h
        PinManager.cpp)

target_include_directories(Pin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Pin PUBLIC
        Core
        Platform
        Settings
        Ui
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(Pin PRIVATE /W4 /WX)
endif ()
