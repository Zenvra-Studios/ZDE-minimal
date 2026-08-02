# CMakeLists.txt for UI module.

file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS "*.h" "*.hpp" "*.c" "*.cpp" "*.cc" "*.mm")

if(UI_SOURCES)
    add_library(UI STATIC ${UI_SOURCES})
    target_include_directories(UI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
