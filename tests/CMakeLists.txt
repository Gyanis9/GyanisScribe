# 测试目录逐级镜像 src：src/Core ↔ tests/Core，按目录就能定位到被测模块
# 框架统一 GoogleTest（不引入 Catch2），用例由 gtest_discover_tests 注册给 ctest
find_package(GTest REQUIRED CONFIG)

add_subdirectory(Core)
add_subdirectory(Settings)
