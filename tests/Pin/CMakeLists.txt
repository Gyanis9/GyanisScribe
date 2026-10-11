add_executable(TestPin
        TestPinLayout.cpp
        TestPinTransform.cpp
        TestPinWindow.cpp)

target_link_libraries(TestPin PRIVATE
        Pin
        Settings
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestPin PRIVATE /W4 /WX)
endif ()

add_dependencies(TestPin DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestPin)
