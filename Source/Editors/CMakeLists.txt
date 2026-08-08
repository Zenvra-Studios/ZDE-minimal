# CMakeLists.txt for Editors module.

file(GLOB_RECURSE Editors_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(Editors_SOURCES)
    add_library(Editors SHARED ${Editors_SOURCES})
    target_include_directories(Editors PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
