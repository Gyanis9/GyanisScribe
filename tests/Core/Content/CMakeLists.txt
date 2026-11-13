add_executable(TestCoreContent
        TestCsvWriter.cpp
        TestMarkdownBuilder.cpp
        TestTableDetector.cpp
        TestXlsxWriter.cpp)

target_link_libraries(TestCoreContent PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestCoreContent PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCoreContent DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCoreContent)
