# Pick：屏幕取色。颜色写法与放大镜几何都是纯函数，遮罩只负责画与点
add_library(Pick STATIC
        ColorFormat.h
        ColorFormat.cpp
        PickGeometry.h
        PickGeometry.cpp
        ColorPickerOverlay.h
        ColorPickerOverlay.cpp
        RecentColors.h
        RecentColors.cpp)

target_include_directories(Pick PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Pick PUBLIC
        Core
        Gui
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(Pick PRIVATE /W4 /WX)
endif ()
