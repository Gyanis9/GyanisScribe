# Core：值类型、日志与目录 provider 的静态库；App 与后续模块都只往这里链接一次
add_library(Core STATIC
        Types.h
        SizeText.h
        SizeText.cpp
        Choices.h
        Text.h
        Logger.h
        Logger.cpp
        Paths.h
        Paths.cpp
        ImageFit.h
        ImageFit.cpp
        ImageFormat.h
        ImageFormat.cpp
        OutputDecoration.h
        OutputDecoration.cpp
        ClipboardFile.h
        ClipboardFile.cpp)

target_include_directories(Core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# 由谁消费就由谁 find_package：conan_provider 只在 find_package 被调用的那一刻把 Conan 的导入目标接进来，
# 少这一句，spdlog::spdlog 到 target_link_libraries 时还不存在
find_package(spdlog REQUIRED CONFIG)

# Types 里的 CapturedImage 用 QImage，Paths 用 QStandardPaths，两者分别来自 Gui 与 Core
target_link_libraries(Core PUBLIC
        Qt6::Core
        Qt6::Gui
        spdlog::spdlog)

target_compile_features(Core PUBLIC cxx_std_23)

if (MSVC)
    # /W4 /WX 是提交硬判据：新引入的告警当场失败，不允许「已知但不管」
    target_compile_options(Core PRIVATE /W4 /WX)
endif ()
