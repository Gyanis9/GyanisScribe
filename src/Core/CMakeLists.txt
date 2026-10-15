# Core：领域逻辑的静态库；各子目录在叶子 CMakeLists 里登记自己的源文件，这里只做聚合与对外接口
add_subdirectory(Image)

get_property(CoreSources GLOBAL PROPERTY GyanisScribeCoreSources)
add_library(Core STATIC ${CoreSources})

target_include_directories(Core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(Core PUBLIC
        Base
        Qt6::Core
        Qt6::Gui)

target_compile_features(Core PUBLIC cxx_std_23)

if (MSVC)
    # /W4 /WX 是提交硬判据：新引入的告警当场失败，不允许「已知但不管」
    target_compile_options(Core PRIVATE /W4 /WX)
endif ()
