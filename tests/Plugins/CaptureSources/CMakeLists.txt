add_executable(TestPluginsCaptureSources
        TestQScreenCaptureSource.cpp)

target_link_libraries(TestPluginsCaptureSources PRIVATE
        Plugins
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestPluginsCaptureSources PRIVATE /W4 /WX)
endif ()

add_dependencies(TestPluginsCaptureSources DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestPluginsCaptureSources)
