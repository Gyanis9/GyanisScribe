# src 汇总层：各模块先建成自己的库，这里只做「可执行体 + 运行目录接线」，不重复声明依赖
add_subdirectory(Core)
add_subdirectory(Ui)
add_subdirectory(Platform)
add_subdirectory(Settings)
add_subdirectory(Hotkey)
add_subdirectory(Capture)
add_subdirectory(Annotation)
add_subdirectory(Pin)
add_subdirectory(Ocr)
add_subdirectory(Record)
add_subdirectory(History)
add_subdirectory(App)

# Debug 档的 Qt DLL 带 d 后缀，Release 不带；这条后缀只在 MSVC + Debug 下成立
set(QT_DEBUG_SUFFIX)
if (MSVC AND CMAKE_BUILD_TYPE MATCHES "Debug")
    set(QT_DEBUG_SUFFIX "d")
endif ()

# Qt 的 DLL 与平台插件属外部不可变产物（不由本构建图产出），按规范例外用拷贝命令部署。
# 待部署清单的唯一出处在本层：新增消费者只 add_dependencies 到同一个目标，不再各自拷第二份。
# 路径一律从导入目标现取（$<TARGET_FILE_DIR:>，构建期求值），不读配置期的 CMAKE_PREFIX_PATH：
# 少了一次带预设的 configure（改了 CMakeLists 之后直接 cmake --build 就会撞上），
# 那个变量展开成空串，清单变成 "/bin/Qt6Cored.dll"，整个部署目标失败而源码一行没改。
add_custom_target(DeployQtRuntime ALL
        COMMENT "把 Qt 运行时副本刷进可执行体所在的目录")

add_custom_command(TARGET DeployQtRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:GyanisScribe>/plugins/platforms")

foreach (QtModule Core Gui Widgets Svg)
    add_custom_command(TARGET DeployQtRuntime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:Qt6::${QtModule}>/Qt6${QtModule}${QT_DEBUG_SUFFIX}.dll"
            "$<TARGET_FILE_DIR:GyanisScribe>/")
endforeach ()

add_custom_command(TARGET DeployQtRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:Qt6::Gui>/../plugins/platforms/qwindows${QT_DEBUG_SUFFIX}.dll"
        "$<TARGET_FILE_DIR:GyanisScribe>/plugins/platforms/")

# WIN32 子系统：托盘类程序不该带控制台窗口；WinMain 由 Qt 的 EntryPoint 目标桥回 main()
add_executable(GyanisScribe WIN32
        App/Main.cpp)

target_link_libraries(GyanisScribe PRIVATE
        App
        Core
        Qt6::EntryPointPrivate
        Qt6::Widgets)

if (MSVC)
    target_compile_options(GyanisScribe PRIVATE /W4 /WX)
endif ()

# 拷贝依赖「产物已最新」而不是相反：先摆好 DLL，用例发现那一步才可能跑起来
add_dependencies(GyanisScribe DeployQtRuntime)
