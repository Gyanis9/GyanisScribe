add_executable(TestGuiRecord
        TestRecordingHud.cpp)

target_link_libraries(TestGuiRecord PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestGuiRecord PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiRecord DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestGuiRecord)
