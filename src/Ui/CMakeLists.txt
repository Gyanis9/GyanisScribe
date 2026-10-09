# Ui 这一层当前只有设计令牌（头文件里的常量），用接口目标把 include 路径发出去
add_library(Ui INTERFACE)

target_include_directories(Ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Ui INTERFACE Qt6::Gui)
