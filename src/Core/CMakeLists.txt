# Core：领域逻辑的静态库；App 与后续模块都只往这里链接一次
add_library(Core STATIC
        ImageFit.h
        ImageFit.cpp
        ImageFormat.h
        ImageFormat.cpp
        OutputDecoration.h
        OutputDecoration.cpp
        ClipboardFile.h
        ClipboardFile.cpp)

target_include_directories(Core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(Core PUBLIC
        Base
        Qt6::Core
        Qt6::Gui)

target_compile_features(Core PUBLIC cxx_std_23)

if (MSVC)
    # /W4 /WX 是提交硬判据：新引入的告警当场失败，不允许「已知但不管」
    target_compile_options(Core PRIVATE /W4 /WX)
endif ()
