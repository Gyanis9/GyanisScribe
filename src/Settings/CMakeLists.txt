# Settings：配置存储的库。ISettingsStore 是纯契约，SettingsStore 是唯一实现
add_library(Settings STATIC
        ISettingsStore.h
        SettingsStore.h
        SettingsStore.cpp)

target_include_directories(Settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Settings PUBLIC
        Core
        Qt6::Core)

if (MSVC)
    target_compile_options(Settings PRIVATE /W4 /WX)
endif ()
