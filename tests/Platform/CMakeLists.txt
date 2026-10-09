add_executable(TestPlatform
        TestPlatform.cpp)

target_link_libraries(TestPlatform PRIVATE
        Platform
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestPlatform PRIVATE /W4 /WX)
endif ()

add_dependencies(TestPlatform DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestPlatform)
