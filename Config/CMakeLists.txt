# CMakeLists.txt for Config module.

file(GLOB_RECURSE Config_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Config_SOURCES)
    add_library(Config STATIC ${Config_SOURCES})
    target_include_directories(Config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
