# CMakeLists.txt for Utility module.

file(GLOB_RECURSE Utility_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Utility_SOURCES)
    add_library(ZDEUtility STATIC ${Utility_SOURCES})
    set_target_properties(ZDEUtility PROPERTIES LINKER_LANGUAGE CXX)
    target_include_directories(ZDEUtility PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
