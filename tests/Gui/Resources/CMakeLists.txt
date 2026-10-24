add_executable(TestGuiResources
        TestBrandIcon.cpp
        TestIconProvider.cpp)

target_compile_definitions(TestGuiResources PRIVATE
        GYANIS_BRAND_DIR="${CMAKE_SOURCE_DIR}/src/Gui/Resources/Brand")

target_link_libraries(TestGuiResources PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestGuiResources PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiResources DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestGuiResources)
