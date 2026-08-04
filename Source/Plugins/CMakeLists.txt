# CMakeLists.txt for Plugins module.

set(Plugins_SOURCES
    Editor/EditorPluginManager.cpp
    Editor/EditorPluginManager.h
)

if(Plugins_SOURCES)
    add_library(Plugins STATIC ${Plugins_SOURCES})
    target_include_directories(Plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
