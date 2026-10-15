add_executable(TestCoreRecord
        TestRecording.cpp
        TestCursorHighlight.cpp)

# 编码端到端那几条要真 ffmpeg：链接 Plugins 拿 FfmpegRecordingEncoder 与命令拼装
target_link_libraries(TestCoreRecord PRIVATE
        Core
        Plugins
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestCoreRecord PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCoreRecord DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCoreRecord)
