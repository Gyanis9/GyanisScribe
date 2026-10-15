# Main：程序骨架与主界面（入口 Main.cpp 由 src/CMakeLists 直接编进可执行体，不进本库）
add_library(Main STATIC
        Application.h
        Application.cpp
        AppController.h
        AppController.cpp
        MainWindow.h
        MainWindow.cpp)

target_include_directories(Main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Main PUBLIC
        App
        Core
        Gui
        Platform
        Plugins
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::PrintSupport)

if (MSVC)
    target_compile_options(Main PRIVATE /W4 /WX)
endif ()
