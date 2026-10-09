# 一条不编译源码的风格门禁：扫 src 里的 Qt 占位符写法。
# 混用 {} 与 %1 在编译期什么都看不出来，运行时也只留一条 QString::arg 告警，
# 界面上却会把「选择{}的目录」原样摆给用户——Linux 容器里第一次就是这样撞出来的。
add_test(NAME Style.QtStringPlaceholders
        COMMAND ${CMAKE_COMMAND}
        -DPLACEHOLDER_SCAN_ROOT=${CMAKE_SOURCE_DIR}/src
        -P ${CMAKE_SOURCE_DIR}/scripts/check-string-placeholders.cmake)
