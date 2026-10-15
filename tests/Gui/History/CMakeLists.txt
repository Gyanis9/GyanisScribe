add_executable(TestGuiHistory
        TestHistoryView.cpp)

target_link_libraries(TestGuiHistory PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestGuiHistory PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiHistory DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestGuiHistory)
