# App：应用对象、核心控制器与入口。带 Q_OBJECT 的头文件必须进源文件列表，AUTOMOC 才找得到
add_library(App STATIC
        Application.h
        Application.cpp
        AppController.h
        AppController.cpp
        MainWindow.h
        MainWindow.cpp
        TrayController.h
        TrayController.cpp
        SingleInstance.h
        SingleInstance.cpp
        CrashGuard.h
        CrashGuard.cpp)

target_include_directories(App PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(App PUBLIC
        Annotation
        Capture
        Core
        History
        Hotkey
        Ocr
        Pick
        Pin
        Platform
        Record
        Settings
        Ui
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network)

# 版本号只有一处真源：工程的 project(... VERSION)，代码里通过宏取，不散落字面量
target_compile_definitions(App PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")

if (MSVC)
    target_compile_options(App PRIVATE /W4 /WX)
endif ()
