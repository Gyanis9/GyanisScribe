add_executable(TestCorePrivacy
        TestPiiDetector.cpp
        TestRedactionGeometry.cpp
        TestRedactImage.cpp)

target_link_libraries(TestCorePrivacy PRIVATE
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Gui)

if (MSVC)
    target_compile_options(TestCorePrivacy PRIVATE /W4 /WX)
endif ()

add_dependencies(TestCorePrivacy DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestCorePrivacy)
