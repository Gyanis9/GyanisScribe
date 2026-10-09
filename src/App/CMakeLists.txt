# App：应用对象与入口。Application 带 Q_OBJECT，头文件必须进源文件列表，AUTOMOC 才找得到它
add_library(App STATIC
        Application.h
        Application.cpp)

target_include_directories(App PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(App PUBLIC
        Core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

# 版本号只有一处真源：工程的 project(... VERSION)，代码里通过宏取，不散落字面量
target_compile_definitions(App PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")

if (MSVC)
    target_compile_options(App PRIVATE /W4 /WX)
endif ()
