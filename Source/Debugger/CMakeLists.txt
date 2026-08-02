# CMakeLists.txt for Debugger module.

file(GLOB_RECURSE Debugger_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Debugger_SOURCES)
    add_library(Debugger STATIC ${Debugger_SOURCES})
    target_include_directories(Debugger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
