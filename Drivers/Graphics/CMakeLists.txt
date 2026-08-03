# CMakeLists.txt for Graphics module.

file(GLOB_RECURSE Graphics_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Graphics_SOURCES)
    add_library(Graphics STATIC ${Graphics_SOURCES})
    target_include_directories(Graphics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(Graphics PROPERTIES LINKER_LANGUAGE CXX)
endif()
