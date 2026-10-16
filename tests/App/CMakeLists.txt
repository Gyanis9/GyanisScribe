add_executable(TestShell
        TestShell.cpp
        TestSingleInstance.cpp
        TestCaptureIntent.cpp
        TestCaptureDelivery.cpp
        TestCommandLine.cpp
        TestCustomHotkeys.cpp
        TestTrayClick.cpp
        TestImageFetcher.cpp
        TestUpdateCheck.cpp)

target_link_libraries(TestShell PRIVATE
        Main
        App
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestShell PRIVATE /W4 /WX)
endif ()

add_dependencies(TestShell DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestShell)
