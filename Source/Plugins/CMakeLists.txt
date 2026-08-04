# CMakeLists.txt for Plugins module.

file(GLOB_RECURSE Plugins_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Plugins_SOURCES)
    add_library(Plugins STATIC ${Plugins_SOURCES})
    target_include_directories(Plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
