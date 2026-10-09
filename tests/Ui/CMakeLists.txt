add_executable(TestUi
        TestIconProvider.cpp)

target_link_libraries(TestUi PRIVATE
        Ui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestUi PRIVATE /W4 /WX)
endif ()

add_dependencies(TestUi DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestUi)
