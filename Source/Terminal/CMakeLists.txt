# CMakeLists.txt for Terminal module.

file(GLOB_RECURSE Terminal_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Terminal_SOURCES)
    add_library(Terminal STATIC ${Terminal_SOURCES})
    target_include_directories(Terminal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
