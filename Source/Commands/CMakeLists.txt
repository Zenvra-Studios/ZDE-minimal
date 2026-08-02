# CMakeLists.txt for Commands module.

file(GLOB_RECURSE Commands_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Commands_SOURCES)
    add_library(Commands STATIC ${Commands_SOURCES})
    target_include_directories(Commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
