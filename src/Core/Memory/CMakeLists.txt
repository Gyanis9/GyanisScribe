# Core/Memory：截图记忆库的值类型、查询分词与重复图指纹
set_property(GLOBAL APPEND PROPERTY GyanisScribeCoreSources
        ${CMAKE_CURRENT_LIST_DIR}/MemoryTypes.h
        ${CMAKE_CURRENT_LIST_DIR}/MemoryTypes.cpp
        ${CMAKE_CURRENT_LIST_DIR}/MemoryToken.h
        ${CMAKE_CURRENT_LIST_DIR}/MemoryToken.cpp
        ${CMAKE_CURRENT_LIST_DIR}/DuplicateHash.h
        ${CMAKE_CURRENT_LIST_DIR}/DuplicateHash.cpp)
