# Scan：二维码与条形码识别。后端是 Conan 拉来的 zxing-cpp（Apache-2.0，许可证正文随包发）。
# 找不到后端时函数交回 DependencyMissing——降级分支与真后端是同一个源文件里的两半，都编得到。
add_library(Scan STATIC
        BarcodeDecoder.h
        BarcodeDecoder.cpp)

target_include_directories(Scan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Scan PUBLIC
        Core
        Qt6::Core
        Qt6::Gui)

find_package(ZXing QUIET CONFIG)
if (ZXing_FOUND)
    target_link_libraries(Scan PRIVATE ZXing::ZXing)
    # 宏发给所有消费者：界面拿它决定按钮灰不灰、用例拿它决定断言哪一档，两处各写一份判断就会分家
    target_compile_definitions(Scan PUBLIC GYANISSCRIBE_HAS_ZXING=1)
endif ()

if (MSVC)
    target_compile_options(Scan PRIVATE /W4 /WX)
endif ()
