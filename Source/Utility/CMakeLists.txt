# CMakeLists.txt for Utility module.

file(GLOB_RECURSE Utility_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Utility_SOURCES)
    add_library(Utility STATIC ${Utility_SOURCES})
    set_target_properties(Utility PROPERTIES LINKER_LANGUAGE CXX)
    target_include_directories(Utility PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
