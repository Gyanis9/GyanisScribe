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
add_subdirectory(Pick)
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

# Windows 才需要把 Qt 的 DLL 与平台插件摆到可执行体旁边：Linux 上 .so 由动态链接器按系统目录找，
# 照抄这些拷贝命令会因为找不到 .dll 把构建判红，所以整个清单包在 WIN32 里。
# 目标本身在两档都存在（各测试可执行体都 add_dependencies 到它），非 Windows 上它是个空操作。
if (WIN32)
add_custom_command(TARGET DeployQtRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:GyanisScribe>/plugins/platforms")

# Concurrent 不在这份清单里：Qt 6 的 QtConcurrent::run 是模板代码，跑的是 QtCore 的线程池，
# QFutureWatcher 也在 QtCore 里——实测 Release 成品与容器里的测试二进制都不 import Qt6Concurrent / libQt6Concurrent
# （打包那一步的 windeployqt 同样没拷它）。链 Qt6::Concurrent 只是为了拿到头文件，摆一份没人加载的 DLL 进运行目录是死重量。
# PrintSupport 在清单里：打印对话框（标注窗 Ctrl+P）真加载它，缺一个 DLL 的表现是构建期枚举整个 .exe 报 0xc0000135
foreach (QtModule Core Gui Widgets Svg Network PrintSupport)
    add_custom_command(TARGET DeployQtRuntime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:Qt6::${QtModule}>/Qt6${QtModule}${QT_DEBUG_SUFFIX}.dll"
            "$<TARGET_FILE_DIR:GyanisScribe>/")
endforeach ()

add_custom_command(TARGET DeployQtRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:Qt6::Gui>/../plugins/platforms/qwindows${QT_DEBUG_SUFFIX}.dll"
        "$<TARGET_FILE_DIR:GyanisScribe>/plugins/platforms/")

# 图像格式插件：PNG 编在 Qt6Gui 里，JPG、WebP 与 GIF 是插件。设置页摆出来的档必须真写得出来，
# 否则用户选了那一档就得到一次失败（实测开发树里 imageformats 没摆时 jpg 就是写不出）。
# GIF 是读的一侧要的：「识别图片文件…」的文件过滤器列着 gif，历史里的动图贴图也要读得回来——
# 少这一份时那些入口在开发树与绿色版上都只会报「读不出图」，而官方安装包走 windeployqt（它默认全拷）看不出问题。
# 打包那一路不用管：windeployqt 默认就把 imageformats 带上（这条链没开 --no-imageformats）。
add_custom_command(TARGET DeployQtRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:GyanisScribe>/plugins/imageformats")
foreach (ImageFormatPlugin jpeg webp gif)
    add_custom_command(TARGET DeployQtRuntime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:Qt6::Gui>/../plugins/imageformats/q${ImageFormatPlugin}${QT_DEBUG_SUFFIX}.dll"
            "$<TARGET_FILE_DIR:GyanisScribe>/plugins/imageformats/")
endforeach ()

# 离屏平台只给开发树用：会真建窗口的用例（贴图、遮罩）把 QT_QPA_PLATFORM 设成 offscreen，
# 否则窗口会弹到用户桌面上并抢前台——并行跑用例时这会咬到别的界面测试。
# 缺这个插件时 Qt 不是报错而是弹出模态的「no platform plugin」消息框，ctest 表现为整条挂住。
# 安装器的 staging 清单是另一处（scripts/WindowsPackage.ps1），产品包里不带这一件。
add_custom_command(TARGET DeployQtRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:Qt6::Gui>/../plugins/platforms/qoffscreen${QT_DEBUG_SUFFIX}.dll"
        "$<TARGET_FILE_DIR:GyanisScribe>/plugins/platforms/")
endif ()

# 语言包：Translations/*.ts 是仓库里的真源，.qm 由 lrelease 在构建期产出。它是本构建图产出的产物，
# 所以按规范挂在 ALL 目标上（不是可执行体的 POST_BUILD），改了 .ts 不重新链接可执行体也会刷新副本。
# lrelease 从 Qt 的 LinguistTools 组件取（Qt6::lrelease 导入目标），不用 find_program：
# Qt 的 bin 目录不在 PATH 上，find_program 会静默找不到，于是这条门变成一个永远空操作的目标——
# 实测「207 条用例全绿」的同时语言包根本没生成。
# 容器里那份 Qt 6.4 没装 LinguistTools（apt 的 qt6-tools 不在环境里），QUIET + 目标存在判断让那一档整条跳过：
# 语言包缺席时应用照旧按中文跑，Ui::Language 会报出该把包放在哪儿。
file(GLOB TRANSLATION_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Translations/*.ts")
find_package(Qt6 COMPONENTS LinguistTools QUIET)

add_custom_target(DeployTranslations ALL
        COMMENT "生成语言包并刷进可执行体所在的目录")

if (TARGET Qt6::lrelease AND TRANSLATION_SOURCES)
    add_custom_command(TARGET DeployTranslations POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GyanisScribe>/translations")
    foreach (TranslationSource ${TRANSLATION_SOURCES})
        # 输出名取自 .ts 的本名（GyanisScribe_en.ts → GyanisScribe_en.qm）：
        # Ui::Language 按档位找这个文件名，两边必须是同一个算法
        get_filename_component(TranslationBaseName ${TranslationSource} NAME_WE)
        add_custom_command(TARGET DeployTranslations POST_BUILD
                COMMAND $<TARGET_FILE:Qt6::lrelease> ${TranslationSource}
                        -qm "$<TARGET_FILE_DIR:GyanisScribe>/translations/${TranslationBaseName}.qm"
                COMMENT "lrelease ${TranslationSource}")
    endforeach ()
endif ()

# WIN32 子系统：托盘类程序不该带控制台窗口；WinMain 由 Qt 的 EntryPoint 目标桥回 main()。
# Linux 上没有这套：main() 就是入口，而 Ubuntu 那份 Qt6 也不提供 EntryPointPrivate 这个目标，
# 照抄会在 generate 期报「target was not found」——这一条是容器里真跑出来的，不是推测
if (WIN32)
    add_executable(GyanisScribe WIN32
            App/Main.cpp)
else ()
    add_executable(GyanisScribe
            App/Main.cpp)
endif ()

target_link_libraries(GyanisScribe PRIVATE
        App
        Core
        Qt6::Widgets)

if (WIN32)
    target_link_libraries(GyanisScribe PRIVATE Qt6::EntryPointPrivate)
endif ()

if (MSVC)
    target_compile_options(GyanisScribe PRIVATE /W4 /WX)
endif ()

# 拷贝依赖「产物已最新」而不是相反：先摆好 DLL 与语言包，用例发现那一步才可能跑起来
add_dependencies(GyanisScribe DeployQtRuntime DeployTranslations)
