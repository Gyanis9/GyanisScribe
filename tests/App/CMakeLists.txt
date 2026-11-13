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

add_executable(TestAppStepRecorder
        TestStepRecorderCoordinator.cpp)

target_link_libraries(TestAppStepRecorder PRIVATE
        App
        Core
        Gui
        Platform
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets
        Qt6::Test)

# 与 TestRewindCoordinator 同一原因：QSignalSpy 在 Qt6Test 里，共用的部署清单不含这份 DLL
add_custom_command(TARGET TestAppStepRecorder POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:TestAppStepRecorder>/")

if (MSVC)
    target_compile_options(TestAppStepRecorder PRIVATE /W4 /WX)
endif ()

add_dependencies(TestAppStepRecorder DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestAppStepRecorder)

add_executable(TestAppMemoryCoordinator
        TestMemoryCoordinator.cpp
        TestMemoryImport.cpp)

target_link_libraries(TestAppMemoryCoordinator PRIVATE
        App
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Test)

add_custom_command(TARGET TestAppMemoryCoordinator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:TestAppMemoryCoordinator>/")

if (MSVC)
    target_compile_options(TestAppMemoryCoordinator PRIVATE /W4 /WX)
endif ()

add_dependencies(TestAppMemoryCoordinator DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestAppMemoryCoordinator)

add_executable(TestAppBugReport
        TestBugReportCoordinator.cpp)

target_link_libraries(TestAppBugReport PRIVATE
        App
        Core
        Gui
        Platform
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets
        Qt6::Test)

# 本目标链了 Qt6::Test，而共用的部署清单只摆产品要的几件：缺这份 DLL，可执行体起不来会被发现那一步报成「测试没跑起来」
add_custom_command(TARGET TestAppBugReport POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:TestAppBugReport>/")

if (MSVC)
    target_compile_options(TestAppBugReport PRIVATE /W4 /WX)
endif ()

add_dependencies(TestAppBugReport DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestAppBugReport)

add_executable(TestAppTiledPrivacyScanner
        TestTiledPrivacyScanner.cpp)

target_link_libraries(TestAppTiledPrivacyScanner PRIVATE
        App
        Core
        GTest::gtest
        GTest::gtest_main
        Qt6::Widgets)

if (MSVC)
    target_compile_options(TestAppTiledPrivacyScanner PRIVATE /W4 /WX)
endif ()

add_dependencies(TestAppTiledPrivacyScanner DeployQtRuntime)

include(GoogleTest)
gtest_discover_tests(TestAppTiledPrivacyScanner)
