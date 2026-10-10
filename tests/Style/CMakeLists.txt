# 一条不编译源码的风格门禁：扫 src 里的 Qt 占位符写法。
# 混用 {} 与 %1 在编译期什么都看不出来，运行时也只留一条 QString::arg 告警，
# 界面上却会把「选择{}的目录」原样摆给用户——Linux 容器里第一次就是这样撞出来的。
add_test(NAME Style.QtStringPlaceholders
        COMMAND ${CMAKE_COMMAND}
        -DPLACEHOLDER_SCAN_ROOT=${CMAKE_SOURCE_DIR}/src
        -P ${CMAKE_SOURCE_DIR}/scripts/check-string-placeholders.cmake)

# 界面文案与翻译目录的对照门禁：源码里每一条走 Core::uiText 的文案都必须在 .ts 里有非空译文。
# 少了这条，英文档下漏掉的那几处会安静地显示中文——没人会把每条菜单、每个报错都点一遍发现它。
add_test(NAME Style.TranslationCatalog
        COMMAND ${CMAKE_COMMAND}
        -DTRANSLATION_SCAN_ROOT=${CMAKE_SOURCE_DIR}/src
        -DTRANSLATION_CATALOG=${CMAKE_SOURCE_DIR}/Translations/GyanisScribe_en.ts
        -P ${CMAKE_SOURCE_DIR}/scripts/check-translation-catalog.cmake)
