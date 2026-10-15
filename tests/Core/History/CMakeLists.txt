add_executable(TestCoreHistory
        TestHistory.cpp
        TestUnsavedArchive.cpp)

target_link_libraries(TestCoreHistory PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestCoreHistory PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCoreHistory DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCoreHistory)
