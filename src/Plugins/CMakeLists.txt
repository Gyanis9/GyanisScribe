# Plugins：可替换后端实现（条码、抓帧来源、识别引擎、录屏编码各占一个子目录）；
# 接口在 Core，实现在这里，装配层（App/Main）决定用哪一个
add_subdirectory(Barcode)
add_subdirectory(CaptureSources)

get_property(PluginsSources GLOBAL PROPERTY GyanisScribePluginsSources)
add_library(Plugins STATIC ${PluginsSources})

target_include_directories(Plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Plugins PUBLIC
        Core
        Qt6::Core
        Qt6::Gui)

# Barcode：二维码与条形码识别。后端是 Conan 拉来的 zxing-cpp（Apache-2.0，许可证正文随包发）。
# 找不到后端时函数交回 DependencyMissing——降级分支与真后端是同一个源文件里的两半，都编得到。
find_package(ZXing QUIET CONFIG)
if (ZXing_FOUND)
    target_link_libraries(Plugins PRIVATE ZXing::ZXing)
    # 宏发给所有消费者：界面拿它决定按钮灰不灰、用例拿它决定断言哪一档，两处各写一份判断就会分家
    target_compile_definitions(Plugins PUBLIC GYANISSCRIBE_HAS_ZXING=1)
endif ()

# DXGI 桌面重复是 Windows + MSVC 这一档的实现：条件分支由平台决定，
# 而不是「可选的第三方依赖」（与 Ocr 的 WinRT 后端同一条规矩）
if (WIN32 AND MSVC)
    # com_ptr 这些投影头 vendor 在 vendor/WinRt：SYSTEM + /external:W0，生成头里的告警不进零告警门禁
    target_include_directories(Plugins SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/WinRt")
    target_compile_options(Plugins PRIVATE /external:W0)
    # 桌面重复的接口在 dxgi，设备与暂存纹理在 d3d11，winrt 运行时伞库随系统
    target_link_libraries(Plugins PRIVATE d3d11 dxgi windowsapp)
endif ()

if (MSVC)
    target_compile_options(Plugins PRIVATE /W4 /WX)
endif ()
