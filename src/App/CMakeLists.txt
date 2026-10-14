# App：应用对象、核心控制器与入口。带 Q_OBJECT 的头文件必须进源文件列表，AUTOMOC 才找得到
add_library(App STATIC
        Application.h
        Application.cpp
        AppController.h
        AppController.cpp
        CommandLine.h
        CommandLine.cpp
        CaptureIntent.h
        CaptureIntent.cpp
        CaptureDelivery.h
        CaptureDelivery.cpp
        CustomHotkeys.h
        CustomHotkeys.cpp
        ClipboardPin.h
        ClipboardPin.cpp
        MainWindow.h
        MainWindow.cpp
        TrayController.h
        TrayController.cpp
        UpdateCheck.h
        UpdateCheck.cpp
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
        Scan
        Settings
        Ui
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::PrintSupport)

# 版本号只有一处真源：工程的 project(... VERSION)，代码里通过宏取，不散落字面量。
# PUBLIC：最终可执行体的 Main.cpp 也用这一份（--version 在应用对象之前就要打出来）
target_compile_definitions(App PUBLIC PROJECT_VERSION="${PROJECT_VERSION}")

if (MSVC)
    target_compile_options(App PRIVATE /W4 /WX)
endif ()

# 崩溃转储要 MiniDumpWriteDump，它在 dbghelp 里（Windows 才有；非 Windows 那一条分支是空操作）
if (WIN32)
    target_link_libraries(App PUBLIC dbghelp)
endif ()
