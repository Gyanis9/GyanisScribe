# 一条不编译源码的风格门禁：扫 src 里的 Qt 占位符写法。
# 混用 {} 与 %1 在编译期什么都看不出来，运行时也只留一条 QString::arg 告警，
# 界面上却会把「选择{}的目录」原样摆给用户。
add_test(NAME Style.QtStringPlaceholders
        COMMAND ${CMAKE_COMMAND}
        -DPLACEHOLDER_SCAN_ROOT=${CMAKE_SOURCE_DIR}/src
        -P ${CMAKE_SOURCE_DIR}/scripts/check-string-placeholders.cmake)

find_program(PYTHON3_EXECUTABLE NAMES python3 python)

# 界面文案与翻译目录的对照门禁：源码里每一条走 Core::uiText 的文案都必须在 .ts 里有非空译文。
# 少了这条，英文档下漏掉的那几处会安静地显示中文——没人会把每条菜单、每个报错都点一遍发现它。
# 用 Python 而不是 CMake 脚本：穷举要把「被 clang-format 断成相邻字面量的长文案」按编译期的规则拼回去，
# CMake 的正则在跨语句时给出了错误的匹配（实测把十条不相关的文案拼成一条），而这条门一旦误判就是永久假绿。
if (PYTHON3_EXECUTABLE)
    add_test(NAME Style.TranslationCatalog
            COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check-translation-catalog.py
                    --scan-root ${CMAKE_SOURCE_DIR}/src
                    --catalog ${CMAKE_SOURCE_DIR}/translations/GyanisScribe_en.ts)

    # 源码约定门禁：路径不许走窄串（Windows 上码页装不下的字会当场抛 std::system_error，
    # 抛出点还在承诺返回 expected 的通道里），以及按枚举分派的开关要覆盖每个值
    # （MSVC 不报这种漏项，漏一支就是菜单上一条按了没反应的项）。
    add_test(NAME Style.SourceConventions
            COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check-source-conventions.py
                    --scan-root ${CMAKE_SOURCE_DIR}/src)
else ()
    # 没有 Python 就当场说明而不是静默不跑：一条永远不会运行的检查比没有检查更糟
    message(WARNING "没找到 python3：Style.TranslationCatalog 与 Style.SourceConventions 这两条门禁不会注册，界面文案对照与路径窄串没人管")
endif ()
