add_executable(TestGuiPin
        TestPinManager.cpp
        TestPinGroupManager.cpp
        TestPinWindow.cpp)

# 两帧 GIF 那条小素材住在 tests/Core/Pin/PinTestAssets.h：拖放里的动图分支与布局那几条共用它，
# 不在两处各打一份字节（两份会随着将来改素材而分家）
target_include_directories(TestGuiPin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Pin)

target_link_libraries(TestGuiPin PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestGuiPin PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiPin DeployQtRuntime)

include(GoogleTest)
# 布局缓存与系统剪贴板两把锁：与 tests/Core/Pin 及别的借用剪贴板的套件同拿，
# 否则并行跑时一个进程删掉的文件正是另一个进程要读的、剪贴板也会被互相覆盖。
# PROPERTIES 的值里分号要转义——裸分号会被 CMake 当列表分隔吃掉（旧账）。
gtest_discover_tests(TestGuiPin PROPERTIES RESOURCE_LOCK "pinLayoutCache\;clipboardShared")
