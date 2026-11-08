find_package(Qt6 COMPONENTS Test REQUIRED)

add_executable(TestGuiAnnotation
        TestAnnotationToolbar.cpp
        TestAnnotationWindow.cpp)

target_link_libraries(TestGuiAnnotation PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Test)

if (MSVC)
    target_compile_options(TestGuiAnnotation PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiAnnotation DeployQtRuntime)

# QSignalSpy 在 Qt6Test 里，而共用产出目录的那份部署清单只摆产品要的几件；
# 少了这一份 DLL，用例可执行体起不来会被发现那一步报成「测试没跑起来」
add_custom_command(TARGET TestGuiAnnotation POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:TestGuiAnnotation>/")

include(GoogleTest)
gtest_discover_tests(TestGuiAnnotation)
