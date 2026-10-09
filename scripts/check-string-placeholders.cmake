# 扫源码里的 QString::arg 占位符写法：Qt 的占位符是 %1..%N，不是 std::format 的 {}。
# 混用的症状是界面上原样显示「选择{}的目录」，而运行期只留一条 QString::arg: Argument missing
# 的告警——Linux 容器里第一次跑起设置页就是这样撞出来的，所以把它钉成门禁而不是口头约定。
#
# 用法：cmake -DPLACEHOLDER_SCAN_ROOT=<仓库>/src -P scripts/check-string-placeholders.cmake
#       tests/Style 把它包成一条 ctest 用例（Style.QtStringPlaceholders）

set(ROOT "${PLACEHOLDER_SCAN_ROOT}")
if (NOT ROOT)
    message(FATAL_ERROR "要用 -DPLACEHOLDER_SCAN_ROOT=<仓库的 src 目录> 调这条脚本")
endif ()

# 引号现取再拼进模式：在 CMake 参数里直接写引号转义会被拆成两个参数，模式就废了。
# 花括号写成 [{][}]：CMake 把 {} 当区间量词，直接写会让整条模式永不匹配（假绿）。
string(ASCII 34 QUOTE)
string(CONCAT PATTERN
        "QStringLiteral\\("
        "${QUOTE}[^${QUOTE}]*"
        "[{][}]"
        "[^${QUOTE}]*${QUOTE}\\)"
        "[ \\t\\r\\n]*"
        "\\.arg")

# —— 自检：先证明这条模式抓得住该抓的、放得过该放的 ——
# 少了这一段，「0 命中」既可能是代码干净也可能是模式早废了，而后者会一直绿
set(PROBE_BAD "browse->setAccessibleName(QStringLiteral(${QUOTE}选择{}的目录${QUOTE}).arg(title));")
string(REGEX MATCHALL "${PATTERN}" BAD_HITS "${PROBE_BAD}")
if (NOT BAD_HITS)
    message(FATAL_ERROR "自检失败：探针里的错误写法没被抓到，这条检查等于没有。模式：${PATTERN}")
endif ()

set(PROBE_GOOD "browse->setAccessibleName(QStringLiteral(${QUOTE}选择%1的目录${QUOTE}).arg(title));")
string(REGEX MATCHALL "${PATTERN}" GOOD_HITS "${PROBE_GOOD}")
if (GOOD_HITS)
    message(FATAL_ERROR "自检失败：模式把正确的 %1 写法也判红，这条检查会天天误报")
endif ()

file(GLOB_RECURSE SOURCES "${ROOT}/*.cpp" "${ROOT}/*.h")
list(LENGTH SOURCES SOURCE_COUNT)
if (SOURCE_COUNT EQUAL 0)
    # 扫到空目录也算失败：一条永远看不到内容的检查比没有检查更糟
    message(FATAL_ERROR "在 ${ROOT} 下没找到任何 .cpp/.h，这条检查没在扫任何东西")
endif ()

set(BAD "")
foreach (SOURCE ${SOURCES})
    file(READ "${SOURCE}" CONTENT)
    string(REGEX MATCHALL "${PATTERN}" HITS "${CONTENT}")
    if (HITS)
        list(APPEND BAD "${SOURCE}: ${HITS}")
    endif ()
endforeach ()

if (BAD)
    list(JOIN BAD "\n  " TEXT)
    message(FATAL_ERROR "以下位置把 Qt 的 %1 写成了 std::format 的 {}：\n  ${TEXT}\n  界面上会原样显示花括号，运行期还会丢掉参数")
endif ()

message(STATUS "Qt 占位符写法检查通过：${ROOT} 下 ${SOURCE_COUNT} 个文件，自检双向都对")
