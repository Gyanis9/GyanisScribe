# Base：无业务的公共底座（值类型、日志、目录、文案取值、尺寸文本、词表通用件、外部命令）。
# 谁都可以依赖它，它不依赖任何业务模块。
add_library(Base STATIC
        Types.h
        Logger.h
        Logger.cpp
        Paths.h
        Paths.cpp
        Text.h
        SizeText.h
        SizeText.cpp
        Choices.h
        ExternalCommand.h
        ExternalCommand.cpp)

target_include_directories(Base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# 由谁消费就由谁 find_package：conan_provider 只在 find_package 被调用的那一刻把 Conan 的导入目标接进来，
# 少这一句，spdlog::spdlog 到 target_link_libraries 时还不存在
find_package(spdlog REQUIRED CONFIG)

# Types 里的 CapturedImage 用 QImage，Paths 用 QStandardPaths，两者分别来自 Gui 与 Core
target_link_libraries(Base PUBLIC
        Qt6::Core
        Qt6::Gui
        spdlog::spdlog)

target_compile_features(Base PUBLIC cxx_std_23)

if (MSVC)
    # /W4 /WX 是提交硬判据：新引入的告警当场失败，不允许「已知但不管」
    target_compile_options(Base PRIVATE /W4 /WX)
endif ()
