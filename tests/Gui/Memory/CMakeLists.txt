find_package(Qt6 COMPONENTS Test REQUIRED)

add_executable(TestGuiMemory
        TestMemoryView.cpp)

target_link_libraries(TestGuiMemory PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets
        Qt6::Gui
        Qt6::Test)

add_custom_command(TARGET TestGuiMemory POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:TestGuiMemory>/")

if (MSVC)
    target_compile_options(TestGuiMemory PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiMemory DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestGuiMemory)
