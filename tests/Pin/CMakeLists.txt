add_executable(TestPin
        TestPinLayout.cpp
        TestPinManager.cpp
        TestPinGroupManager.cpp
        TestPinTransform.cpp
        TestPinViewFilter.cpp
        TestPinWindow.cpp)

target_link_libraries(TestPin PRIVATE
        Pin
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestPin PRIVATE /W4 /WX)
endif ()

add_dependencies(TestPin DeployQtRuntime)

include(GoogleTest)
# 布局那三条用例写的是真实缓存目录（cacheDirectory()/Pins），目录名由 Pin 模块自己定，测试改不了；
# ctest 把每条用例当独立进程并行起（同一个可执行体里的用例也会各起一个进程），
# 因此这一组要按资源锁串行，否则一个进程删掉的文件是另一个进程正要读的。
# 锁名与 tests/App 里 TestModuleFlow 的是同一把：跨二进制的互斥只在两边都拿它时成立。
# clipboardShared 是同一条道理的另一份账：动图与直接标注那几条用例借系统剪贴板
# 传图（写一份再读回来），并行时读到的会是别的进程刚写进去的另一张图——
# 表现是「像素越界读到无效色」这种看着像坐标写错的假红。三处碰剪贴板的用例
# （这里、TestCapture 的选区复制、TestOcr 的结果复制）同拿这一把锁。
# 注意：PROPERTIES 的值里两把锁之间的分号要写成 \;（转义）——裸分号会被 CMake 当列表分隔吃掉，只剩第一把。
gtest_discover_tests(TestPin PROPERTIES RESOURCE_LOCK "pinLayoutCache\;clipboardShared")
