# Gui/Resources：qrc 与 Icons/Styles 资产、资源装载器、图标供给器
set_property(GLOBAL APPEND PROPERTY GyanisScribeGuiSources
        ${CMAKE_CURRENT_LIST_DIR}/AppResources.h
        ${CMAKE_CURRENT_LIST_DIR}/AppResources.cpp
        ${CMAKE_CURRENT_LIST_DIR}/IconProvider.h
        ${CMAKE_CURRENT_LIST_DIR}/IconProvider.cpp
        ${CMAKE_CURRENT_LIST_DIR}/AppResources.qrc)
