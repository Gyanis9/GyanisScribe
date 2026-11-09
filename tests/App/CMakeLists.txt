find_package(Qt6 COMPONENTS Test REQUIRED)

add_executable(TestShell
        TestShell.cpp
        TestSingleInstance.cpp
        TestCaptureIntent.cpp
        TestCaptureDelivery.cpp
        TestDeliveryCoordinator.cpp
        TestHistoryCoordinator.cpp
        TestCommandLine.cpp
        TestCustomHotkeys.cpp
        TestTrayClick.cpp
        TestImageFetcher.cpp
        TestUpdateCheck.cpp)

target_link_libraries(TestShell PRIVATE
        Main
        App
        Gui
        Core
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestShell PRIVATE /W4 /WX)
endif ()

add_dependencies(TestShell DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestShell)

add_executable(TestRecordingCoordinator
        TestRecordingCoordinator.cpp)

target_link_libraries(TestRecordingCoordinator PRIVATE
        App
        GTest::gtest
        GTest::gtest_main)

if (MSVC)
    target_compile_options(TestRecordingCoordinator PRIVATE /W4 /WX)
endif ()

add_dependencies(TestRecordingCoordinator DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestRecordingCoordinator)

add_executable(TestRewindCoordinator
        TestRewindCoordinator.cpp)

target_link_libraries(TestRewindCoordinator PRIVATE
        App
        Core
        Gui
        Platform
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets
        Qt6::Test)

# QSignalSpy 在 Qt6Test 里，而共用产出目录的那份部署清单只摆产品要的几件；
# 少了这一份 DLL，用例可执行体起不来会被发现那一步报成「测试没跑起来」
add_custom_command(TARGET TestRewindCoordinator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:TestRewindCoordinator>/")

if (MSVC)
    target_compile_options(TestRewindCoordinator PRIVATE /W4 /WX)
endif ()

add_dependencies(TestRewindCoordinator DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestRewindCoordinator)

add_executable(TestPrivacyCoordinator
        TestPrivacyCoordinator.cpp)

target_link_libraries(TestPrivacyCoordinator PRIVATE
        App
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Test)

# 与 TestRewindCoordinator 同一原因：QSignalSpy 在 Qt6Test 里，共用的部署清单不含这份 DLL
add_custom_command(TARGET TestPrivacyCoordinator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:TestPrivacyCoordinator>/")

if (MSVC)
    target_compile_options(TestPrivacyCoordinator PRIVATE /W4 /WX)
endif ()

add_dependencies(TestPrivacyCoordinator DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestPrivacyCoordinator)
