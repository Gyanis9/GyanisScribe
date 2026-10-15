add_executable(TestCorePin
        TestPinLayout.cpp
        TestPinTransform.cpp
        TestPinViewFilter.cpp)

target_link_libraries(TestCorePin PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestCorePin PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCorePin DeployQtRuntime)

include(GoogleTest)
# 布局那几条用例写的是真实缓存目录（cacheDirectory()/Pins），目录名由 Core 的贴图布局自己定，测试改不了；
# 与 Gui 侧的贴图用例同拿一把锁：跨二进制的互斥只在两边都拿它时成立。
gtest_discover_tests(TestCorePin PROPERTIES RESOURCE_LOCK "pinLayoutCache\;clipboardShared")
