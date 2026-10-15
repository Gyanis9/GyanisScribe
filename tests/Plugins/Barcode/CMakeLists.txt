add_executable(TestPluginsBarcode
        TestBarcodeDecoder.cpp)

target_link_libraries(TestPluginsBarcode PRIVATE
        Plugins
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui)

# 判据自造二维码样例，要用后端自己的编码件：Plugins 那边是 PRIVATE 连接，头不会传过来，
# 这里单独连一次。宏 GYANISSCRIBE_HAS_ZXING 由 Plugins 以 PUBLIC 传出，两处判断同解
find_package(ZXing QUIET CONFIG)
if (ZXing_FOUND)
    target_link_libraries(TestPluginsBarcode PRIVATE ZXing::ZXing)
endif ()

if (MSVC)
    target_compile_options(TestPluginsBarcode PRIVATE /W4 /WX)
endif ()

gtest_discover_tests(TestPluginsBarcode)
