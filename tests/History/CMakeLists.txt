add_executable(TestHistory
        TestHistory.cpp)

target_link_libraries(TestHistory PRIVATE
        History
        Settings
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Core)

if (MSVC)
    target_compile_options(TestHistory PRIVATE /W4 /WX)
endif ()

add_dependencies(TestHistory DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestHistory)
