# App：应用服务集（托盘、单实例、命令行、崩溃守护、更新检查、内容投递与自定义热键）。
# 带 Q_OBJECT 的头文件必须进源文件列表，AUTOMOC 才找得到
add_library(App STATIC
        CommandLine.h
        CommandLine.cpp
        CaptureIntent.h
        CaptureIntent.cpp
        CaptureDelivery.h
        CaptureDelivery.cpp
        ImageFetcher.h
        ImageFetcher.cpp
        CustomHotkeys.h
        CustomHotkeys.cpp
        ClipboardPin.h
        ClipboardPin.cpp
        TrayAction.h
        TrayClick.h
        TrayClick.cpp
        TrayController.h
        TrayController.cpp
        UpdateCheck.h
        UpdateCheck.cpp
        SingleInstance.h
        SingleInstance.cpp
        OcrCoordinator.h
        OcrCoordinator.cpp)

target_include_directories(App PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(App PUBLIC
        Core
        Platform
        Plugins
        Gui
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
