add_executable(TestPluginsRecording
        TestFfmpegAudioDevices.cpp
        TestFfmpegVideoEncoders.cpp)

target_link_libraries(TestPluginsRecording PRIVATE
        Plugins
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui)

if (MSVC)
    target_compile_options(TestPluginsRecording PRIVATE /W4 /WX)
endif ()

add_dependencies(TestPluginsRecording DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestPluginsRecording)

# 这一组用例走的是服务本体：开始一轮就在缓存的 Recording 目录下建带时间戳的帧目录，收尾时用例要把
# 自己那一份清掉。ctest -j 会把每条用例当成独立进程，共删同一棵树就会互相删掉对方正在写的帧
# （实测并行跑两条红、单跑全绿），因此整组串行。
add_executable(TestPluginsRecordingService
        TestRecordingService.cpp)

target_link_libraries(TestPluginsRecordingService PRIVATE
        Plugins
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestPluginsRecordingService PRIVATE /W4 /WX)
endif ()

add_dependencies(TestPluginsRecordingService DeployQtRuntime)

gtest_discover_tests(TestPluginsRecordingService PROPERTIES RESOURCE_LOCK recordingCache)
