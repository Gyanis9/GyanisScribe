add_executable(TestShell
        TestShell.cpp)

target_link_libraries(TestShell PRIVATE
        App
        Settings
        Ui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestShell PRIVATE /W4 /WX)
endif ()

add_dependencies(TestShell DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestShell)
