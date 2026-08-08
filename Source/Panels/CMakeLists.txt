# CMakeLists.txt for Panels module.

file(GLOB_RECURSE Panels_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Panels_SOURCES)
    add_library(Panels SHARED ${Panels_SOURCES})
    target_include_directories(Panels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
