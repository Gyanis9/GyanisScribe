add_executable(TestCoreMemory
        TestMemoryToken.cpp
        TestDuplicateHash.cpp)

target_link_libraries(TestCoreMemory PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestCoreMemory PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCoreMemory DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCoreMemory)
