cmake_minimum_required(VERSION 3.28)
project(GyanisScribe VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 只有 AUTOMOC 有真实消费者（带 Q_OBJECT 的类）；AUTORCC 等 .qrc 落地那一批再开，AUTOUIC 全程用不上
set(CMAKE_AUTOMOC ON)

# Qt 套件位置由环境变量 QTDIR 提供（构建脚本按本机档案设置），不在工程里写死绝对路径
if (DEFINED ENV{QTDIR})
    set(CMAKE_PREFIX_PATH "$ENV{QTDIR}")
endif ()

# 主程序与测试可执行体共用一个输出目录：Qt 运行时只需部署一份，
# 否则 gtest 的构建期用例发现会在缺 DLL 的那一侧报成「测试没跑起来」
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        Svg
        REQUIRED)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
