# Pin：贴图。纯几何与布局存取可无界面测试；窗口本身的键盘与双击由 tests/Pin/TestPinWindow.cpp 直投事件来测
# （构造要一个 QApplication，但不必真把窗口摆上屏幕，容器那一档在 Xvfb 下跑）
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
        Pick
        Platform
        Settings
        Ui
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(Pin PRIVATE /W4 /WX)
endif ()
