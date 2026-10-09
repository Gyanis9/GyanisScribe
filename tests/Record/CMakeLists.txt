add_executable(TestRecord
        TestRecording.cpp
        TestRecordingHud.cpp)

target_link_libraries(TestRecord PRIVATE
        Record
        Capture
        Core
        Ui
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestRecord PRIVATE /W4 /WX)
endif ()

add_dependencies(TestRecord DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestRecord)
