# 翻译目录的两用脚本：从源码里穷举界面文案，并生成或校验 Translations/*.ts。
#
# 为什么不是 lupdate：本工程的界面文案有一部分存在「按位置对齐的 constexpr 表」里（档位—标签只能有一个真源），
# 显示时经 Core::uiText(view.data()) 运行期查表。lupdate 只认字面量实参，认不出这些；
# 把表改成函数会让每个消费点与每条钉表的用例跟着改。于是这里自己穷举，并把它变成一道门禁：
# 源码用到的文案若不在目录里，英文档就会露出中文——这条检查就是把那种露出钉红。
#
# 用法（校验，tests/Style 把它包成 ctest 用例 Style.TranslationCatalog）：
#   cmake -DTRANSLATION_SCAN_ROOT=<仓库>/src -DTRANSLATION_CATALOG=<仓库>/Translations/GyanisScribe_en.ts \
#         -P scripts/check-translation-catalog.cmake
# 用法（生成骨架，保留已有译文）：再加 -DTRANSLATION_CATALOG_GENERATE=ON
set(ROOT "${TRANSLATION_SCAN_ROOT}")
set(CATALOG "${TRANSLATION_CATALOG}")
if (NOT ROOT OR NOT CATALOG)
    message(FATAL_ERROR "要用 -DTRANSLATION_SCAN_ROOT=<仓库的 src 目录> -DTRANSLATION_CATALOG=<.ts 路径> 调这条脚本")
endif ()

file(GLOB_RECURSE SOURCES "${ROOT}/*.cpp" "${ROOT}/*.h")
list(LENGTH SOURCES SOURCE_COUNT)
if (NOT SOURCES)
    message(FATAL_ERROR "在 ${ROOT} 下没找到任何 .cpp/.h，这条检查等于没有（它会一直绿）")
endif ()

# 把 XML 文本还原成源码里的那串字符（生成与校验用同一个函数，两边才不会各说各话）
function(unescape_xml TEXT OUT_VAR)
    set(RESULT "${TEXT}")
    string(REPLACE "&lt;" "<" RESULT "${RESULT}")
    string(REPLACE "&gt;" ">" RESULT "${RESULT}")
    string(REPLACE "&quot;" "\"" RESULT "${RESULT}")
    string(REPLACE "&amp;" "&" RESULT "${RESULT}")
    set(${OUT_VAR} "${RESULT}" PARENT_SCOPE)
endfunction()

function(escape_xml TEXT OUT_VAR)
    set(RESULT "${TEXT}")
    string(REPLACE "&" "&amp;" RESULT "${RESULT}")
    string(REPLACE "<" "&lt;" RESULT "${RESULT}")
    string(REPLACE ">" "&gt;" RESULT "${RESULT}")
    string(REPLACE "\"" "&quot;" RESULT "${RESULT}")
    set(${OUT_VAR} "${RESULT}" PARENT_SCOPE)
endfunction()

# 在目录里按字面找一条 <source>，并取它后面那条 <translation> 的内容。
# 刻意不用 regex 匹配 source：文案里有 ( * + 这类字符（例如「文本文件 (*.txt)」），
# 拼进模式就会让 CMake 的 regex 编译失败——门禁自己坏掉比检查不到更糟。
# 做法是先 string(FIND) 定位，再截一段固定窗口，只对这一段跑 regex。
function(find_catalog_entry CATALOG_TEXT SOURCE_ESCAPED FOUND_VAR TRANSLATION_VAR UNFINISHED_VAR)
    set(${FOUND_VAR} FALSE PARENT_SCOPE)
    set(${TRANSLATION_VAR} "" PARENT_SCOPE)
    set(${UNFINISHED_VAR} "" PARENT_SCOPE)

    string(FIND "${CATALOG_TEXT}" "<source>${SOURCE_ESCAPED}</source>" POSITION)
    if (POSITION LESS 0)
        return()
    endif ()
    string(SUBSTRING "${CATALOG_TEXT}" "${POSITION}" 1200 CHUNK)
    string(REGEX MATCH "<translation([^>]*)>([^<]*)</translation>" ENTRY "${CHUNK}")
    if (NOT ENTRY)
        return()
    endif ()
    set(${FOUND_VAR} TRUE PARENT_SCOPE)
    set(${UNFINISHED_VAR} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    unescape_xml("${CMAKE_MATCH_2}" PLAIN)
    set(${TRANSLATION_VAR} "${PLAIN}" PARENT_SCOPE)
endfunction()

# C++ 里的转义还原成真实文本：\" 与 \\（目录的键必须是运行期那份字节，否则查不中）
function(unescape_cpp TEXT OUT_VAR)
    set(RESULT "${TEXT}")
    string(REPLACE "\\\"" "\"" RESULT "${RESULT}")
    string(REPLACE "\\\\" "\\" RESULT "${RESULT}")
    set(${OUT_VAR} "${RESULT}" PARENT_SCOPE)
endfunction()

set(REQUIRED "")
set(MALFORMED "")
foreach (SOURCE ${SOURCES})
    file(READ "${SOURCE}" CONTENT)
    # 相邻字面量拼接（uiText("a" "b")）这条脚本不支持：它只会取到第一片，
    # 于是目录里会出现一条半截文案。当场报出来，让调用点改成单条字面量。
    string(REGEX MATCHALL "uiText[A-Za-z]*\\([ \t\r\n]*\"[^\"]*\"[ \t\r\n]*\"" JOINED "${CONTENT}")
    if (JOINED)
        list(APPEND MALFORMED "${SOURCE}: 相邻字面量拼接 ${JOINED}")
    endif ()
    string(REGEX MATCHALL "uiText[A-Za-z]*\\([ \t\r\n]*\"((\\\\.|[^\"\\\\])*)\"" HITS "${CONTENT}")
    foreach (HIT ${HITS})
        string(REGEX REPLACE "uiText[A-Za-z]*\\([ \t\r\n]*\"((\\\\.|[^\"\\\\])*)\".*" "\\1" RAW "${HIT}")
        unescape_cpp("${RAW}" TEXT_VALUE)
        list(APPEND REQUIRED "${TEXT_VALUE}")
    endforeach ()

    # 显示标签还有一类不走字面量实参：它们存在 constexpr 的聚合表里（档位—标签同序的那几张），
    # 运行期是 uiText(row.label.data())，本脚本看不见那个值。所以把表行里的中文一并穷举进来——
    # 少收的后果是英文档露出中文（无声），多收只是目录里多条没人用的条目（有声、无害）。
    # 只认「大写开头的聚合名{…}」这一形：随手写的代码块 { Logger::warning("…") } 不是表行，
    # 把日志文案也收进来就会让目录里堆出一批永远不会被查表的条目。
    # 表行必须在一行之内容下：[^{}"] 之外再排除换行，否则两个大括号之间的跨度会吃掉整段代码，
    # 把中间的日志文案也算成表行（实测不收换行时穷举出 216 条，其中 80 多条是这种噪声）。
    string(REGEX MATCHALL "[A-Z][A-Za-z0-9_]*\\{[^{}\n]*\"((\\\\.|[^\"\\\\])*)\"[^{}\n]*\\}" ROWS "${CONTENT}")
    foreach (ROW ${ROWS})
        string(REGEX MATCHALL "\"((\\\\.|[^\"\\\\])*)\"" CELLS "${ROW}")
        foreach (CELL ${CELLS})
            string(REGEX REPLACE "^\"((\\\\.|[^\"\\\\])*)\"$" "\\1" RAW_CELL "${CELL}")
            # 只收含汉字的单元：表里其余字面量是编号、图标名与配置 token
            if (RAW_CELL MATCHES "[一-龥]")
                unescape_cpp("${RAW_CELL}" TEXT_VALUE)
                list(APPEND REQUIRED "${TEXT_VALUE}")
            endif ()
        endforeach ()
    endforeach ()
endforeach ()
list(REMOVE_DUPLICATES REQUIRED)
list(LENGTH REQUIRED REQUIRED_COUNT)

if (MALFORMED)
    list(JOIN MALFORMED "\n  " TEXT)
    message(FATAL_ERROR "以下 uiText 调用把文案拆成了相邻字面量，本脚本只能读到第一片：\n  ${TEXT}\n  改成一条字面量（长文案可以换行写在同一条里）")
endif ()

if (NOT REQUIRED)
    message(FATAL_ERROR "在 ${ROOT} 下没找到任何 Core::uiText 文案，检查接线是不是断了（源码里一个界面文案都没走这条口？）")
endif ()

# 自检：穷举必须真抓到东西，且模式不能把非文案的调用也算进来
list(LENGTH REQUIRED PROBE_COUNT)
if (PROBE_COUNT LESS 3)
    message(FATAL_ERROR "只穷举到 ${PROBE_COUNT} 条文案，怀疑模式失效")
endif ()

if (TRANSLATION_CATALOG_GENERATE)
    # 生成模式：读旧目录里的译文，按新的必需清单重写，已有译文原样带过去
    set(EXISTING "")
    if (EXISTS "${CATALOG}")
        file(READ "${CATALOG}" OLD_CATALOG)
        set(EXISTING "${OLD_CATALOG}")
    endif ()

    set(ENTRY_TEXT "")
    set(NEW_COUNT 0)
    foreach (TEXT_VALUE ${REQUIRED})
        escape_xml("${TEXT_VALUE}" ESCAPED)
        # 旧目录里有同一 <source> 且已带译文就保留；没有就留空并标 unfinished
        set(TRANSLATION_TEXT "")
        if (EXISTING)
            find_catalog_entry("${EXISTING}" "${ESCAPED}" FOUND TRANSLATION_TEXT UNFINISHED_FLAG)
            if (UNFINISHED_FLAG MATCHES "unfinished")
                set(TRANSLATION_TEXT "")
            endif ()
        endif ()
        if (TRANSLATION_TEXT STREQUAL "")
            string(APPEND ENTRY_TEXT "        <message>\n            <source>${ESCAPED}</source>\n            <translation type=\"unfinished\"></translation>\n        </message>\n")
            math(EXPR NEW_COUNT "${NEW_COUNT} + 1")
        else ()
            escape_xml("${TRANSLATION_TEXT}" ESCAPED_TRANSLATION)
            string(APPEND ENTRY_TEXT "        <message>\n            <source>${ESCAPED}</source>\n            <translation>${ESCAPED_TRANSLATION}</translation>\n        </message>\n")
        endif ()
    endforeach ()

    file(WRITE "${CATALOG}" "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<!DOCTYPE TS>\n"
            "<TS version=\"2.1\" language=\"en_US\">\n"
            "<context>\n"
            "    <name>GyanisScribe</name>\n"
            "${ENTRY_TEXT}"
            "</context>\n"
            "</TS>\n")
    message(STATUS "翻译目录已生成：${CATALOG}，必需文案 ${REQUIRED_COUNT} 条（其中空白译文 ${NEW_COUNT} 条）")
    return()
endif ()

# 校验模式：目录里必须有每一条必需文案，且不能是 unfinished
if (NOT EXISTS "${CATALOG}")
    message(FATAL_ERROR "翻译目录 ${CATALOG} 不存在：先跑 cmake -DTRANSLATION_CATALOG_GENERATE=ON -P scripts/check-translation-catalog.cmake 生成")
endif ()
file(READ "${CATALOG}" CATALOG_TEXT)

set(MISSING "")
set(UNFINISHED "")
foreach (TEXT_VALUE ${REQUIRED})
    escape_xml("${TEXT_VALUE}" ESCAPED)
    find_catalog_entry("${CATALOG_TEXT}" "${ESCAPED}" FOUND TRANSLATED UNFINISHED_FLAG)
    if (NOT FOUND)
        list(APPEND MISSING "${TEXT_VALUE}")
    elseif (UNFINISHED_FLAG MATCHES "unfinished" OR TRANSLATED MATCHES "^[ \t\r\n]*$")
        list(APPEND UNFINISHED "${TEXT_VALUE}")
    endif ()
endforeach ()

if (MISSING OR UNFINISHED)
    set(REPORT "")
    if (MISSING)
        list(JOIN MISSING "\n  " TEXT)
        string(APPEND REPORT "\n  目录里完全没有的文案：\n  ${TEXT}")
    endif ()
    if (UNFINISHED)
        list(JOIN UNFINISHED "\n  " TEXT)
        string(APPEND REPORT "\n  有条目但译文还是空的：\n  ${TEXT}")
    endif ()
    list(LENGTH MISSING MISSING_COUNT)
    list(LENGTH UNFINISHED UNFINISHED_COUNT)
    message(FATAL_ERROR "界面文案共 ${REQUIRED_COUNT} 条，缺 ${MISSING_COUNT} 条、空译文 ${UNFINISHED_COUNT} 条。"
            "英文档下这些位置会露出中文：${REPORT}"
            "\n  补译文后重新生成 .qm（构建会自动跑 lrelease）")
endif ()

message(STATUS "翻译目录检查通过：${ROOT} 下 ${SOURCE_COUNT} 个文件，${REQUIRED_COUNT} 条文案全部有译文")
