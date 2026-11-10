add_executable(TestGuiShare
        TestShareDialog.cpp)

target_link_libraries(TestGuiShare PRIVATE
        Gui
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestGuiShare PRIVATE /W4 /WX)
endif ()

add_dependencies(TestGuiShare DeployQtRuntime)

include(GoogleTest)
# 复制那条会真的往系统剪贴板放图：与别的剪贴板套件同拿一把锁
gtest_discover_tests(TestGuiShare PROPERTIES RESOURCE_LOCK clipboardShared)
